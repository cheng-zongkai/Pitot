#include "app/autostart.h"

#include <windows.h>

#include <objbase.h>
#define SECURITY_WIN32
#include <security.h>
#include <taskschd.h>
#include <wrl/client.h>

#include <cwchar>

#include "common/log.h"
#include "common/names.h"

namespace pitot::app {
namespace {

using Microsoft::WRL::ComPtr;

class Bstr {
public:
    explicit Bstr(const wchar_t* text) : s_(SysAllocString(text)) {}
    ~Bstr() { SysFreeString(s_); }
    Bstr(const Bstr&) = delete;
    Bstr& operator=(const Bstr&) = delete;
    operator BSTR() const { return s_; }

private:
    BSTR s_;
};

// 线程里 COM 可能已经按别的模式初始化过（RPC_E_CHANGED_MODE），那种情况直接用，也不负责卸载
class ComScope {
public:
    ComScope() : hr_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~ComScope() {
        if (SUCCEEDED(hr_)) {
            CoUninitialize();
        }
    }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;
    bool ok() const { return SUCCEEDED(hr_) || hr_ == RPC_E_CHANGED_MODE; }

private:
    HRESULT hr_;
};

VARIANT EmptyVariant() {
    VARIANT v;
    VariantInit(&v);
    return v;
}

bool Check(HRESULT hr, const wchar_t* what) {
    if (FAILED(hr)) {
        log::Write(L"autostart: %s failed 0x%08lX", what, static_cast<unsigned long>(hr));
        return false;
    }
    return true;
}

bool IsNotFound(HRESULT hr) {
    return hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
}

ComPtr<ITaskService> Connect() {
    ComPtr<ITaskService> service;
    if (!Check(CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&service)),
               L"CoCreateInstance(TaskScheduler)")) {
        return nullptr;
    }
    const VARIANT empty = EmptyVariant();
    if (!Check(service->Connect(empty, empty, empty, empty), L"ITaskService::Connect")) {
        return nullptr;
    }
    return service;
}

// 任务登记的是登录用户，不是“任何人登录都启动”。用 DOMAIN\user 形式，本地账户 / 微软账户都适用
std::wstring CurrentUser() {
    wchar_t name[256];
    ULONG size = static_cast<ULONG>(std::size(name));
    if (GetUserNameExW(NameSamCompatible, name, &size)) {
        return name;
    }
    return {};
}

std::wstring StripQuotes(std::wstring path) {
    while (!path.empty() && path.front() == L'"') {
        path.erase(path.begin());
    }
    while (!path.empty() && path.back() == L'"') {
        path.pop_back();
    }
    return path;
}

std::wstring DirectoryOf(const wchar_t* path) {
    std::wstring dir = path;
    const size_t slash = dir.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : dir.substr(0, slash);
}

bool Register(ITaskService* service, const wchar_t* exe_path) {
    const std::wstring user = CurrentUser();
    if (user.empty()) {
        log::Write(L"autostart: cannot resolve the current user %lu", GetLastError());
        return false;
    }

    ComPtr<ITaskDefinition> task;
    if (!Check(service->NewTask(0, &task), L"NewTask")) {
        return false;
    }

    ComPtr<IRegistrationInfo> info;
    if (SUCCEEDED(task->get_RegistrationInfo(&info))) {
        info->put_Description(Bstr(L"Starts Pitot when you sign in to Windows."));
    }

    // 最高权限 + 交互式令牌：登录后在用户桌面会话里以管理员运行，不弹 UAC
    ComPtr<IPrincipal> principal;
    if (!Check(task->get_Principal(&principal), L"get_Principal") ||
        !Check(principal->put_UserId(Bstr(user.c_str())), L"put_UserId") ||
        !Check(principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN), L"put_LogonType") ||
        !Check(principal->put_RunLevel(TASK_RUNLEVEL_HIGHEST), L"put_RunLevel")) {
        return false;
    }

    ComPtr<ITaskSettings> settings;
    if (!Check(task->get_Settings(&settings), L"get_Settings")) {
        return false;
    }
    settings->put_Enabled(VARIANT_TRUE);
    // 台式机也可能接 UPS / 笔记本，别因为用电池就不启动或被中途结束
    settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
    settings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
    // 常驻程序：默认 72 小时后会被任务计划结束
    settings->put_ExecutionTimeLimit(Bstr(L"PT0S"));
    settings->put_MultipleInstances(TASK_INSTANCES_IGNORE_NEW);
    // 任务默认优先级 7 会让进程以“低于正常”运行，任务栏条跟着卡，改成正常
    settings->put_Priority(4);

    ComPtr<ITriggerCollection> triggers;
    ComPtr<ITrigger> trigger;
    ComPtr<ILogonTrigger> logon;
    if (!Check(task->get_Triggers(&triggers), L"get_Triggers") ||
        !Check(triggers->Create(TASK_TRIGGER_LOGON, &trigger), L"Triggers::Create") ||
        !Check(trigger.As(&logon), L"QI ILogonTrigger") || !Check(logon->put_UserId(Bstr(user.c_str())), L"trigger UserId")) {
        return false;
    }
    trigger->put_Enabled(VARIANT_TRUE);

    ComPtr<IActionCollection> actions;
    ComPtr<IAction> action;
    ComPtr<IExecAction> exec;
    if (!Check(task->get_Actions(&actions), L"get_Actions") ||
        !Check(actions->Create(TASK_ACTION_EXEC, &action), L"Actions::Create") ||
        !Check(action.As(&exec), L"QI IExecAction") || !Check(exec->put_Path(Bstr(exe_path)), L"put_Path")) {
        return false;
    }
    // 工作目录设成 exe 所在目录，和从资源管理器双击启动一致
    const std::wstring dir = DirectoryOf(exe_path);
    if (!dir.empty()) {
        exec->put_WorkingDirectory(Bstr(dir.c_str()));
    }

    // 文件夹不存在就建
    ComPtr<ITaskFolder> folder;
    HRESULT hr = service->GetFolder(Bstr(names::kAutostartFolder), &folder);
    if (IsNotFound(hr)) {
        ComPtr<ITaskFolder> root;
        if (!Check(service->GetFolder(Bstr(L"\\"), &root), L"GetFolder(root)")) {
            return false;
        }
        // CreateFolder 的参数不带前导反斜杠
        hr = root->CreateFolder(Bstr(names::kAutostartFolder + 1), EmptyVariant(), &folder);
    }
    if (!Check(hr, L"task folder")) {
        return false;
    }

    VARIANT user_variant = EmptyVariant();
    user_variant.vt = VT_BSTR;
    user_variant.bstrVal = SysAllocString(user.c_str());
    ComPtr<IRegisteredTask> registered;
    const HRESULT register_hr =
        folder->RegisterTaskDefinition(Bstr(names::kAutostartName), task.Get(), TASK_CREATE_OR_UPDATE, user_variant,
                                       EmptyVariant(), TASK_LOGON_INTERACTIVE_TOKEN, EmptyVariant(), &registered);
    VariantClear(&user_variant);
    return Check(register_hr, L"RegisterTaskDefinition");
}

}  // namespace

AutostartState QueryAutostart(const wchar_t* exe_path, std::wstring* registered_path) {
    if (registered_path) {
        registered_path->clear();
    }
    ComScope com;
    if (!com.ok()) {
        return AutostartState::Unknown;
    }
    const ComPtr<ITaskService> service = Connect();
    if (!service) {
        return AutostartState::Unknown;
    }
    ComPtr<ITaskFolder> root;
    ComPtr<IRegisteredTask> registered;
    if (!Check(service->GetFolder(Bstr(L"\\"), &root), L"GetFolder(root)")) {
        return AutostartState::Unknown;
    }
    const HRESULT hr = root->GetTask(Bstr(names::kAutostartTask), &registered);
    if (IsNotFound(hr)) {
        return AutostartState::Off;
    }
    if (!Check(hr, L"GetTask")) {
        return AutostartState::Unknown;
    }

    VARIANT_BOOL enabled = VARIANT_FALSE;
    if (FAILED(registered->get_Enabled(&enabled)) || enabled == VARIANT_FALSE) {
        return AutostartState::Off;
    }

    ComPtr<ITaskDefinition> definition;
    ComPtr<IActionCollection> actions;
    ComPtr<IAction> action;
    ComPtr<IExecAction> exec;
    BSTR raw = nullptr;
    if (FAILED(registered->get_Definition(&definition)) || FAILED(definition->get_Actions(&actions)) ||
        FAILED(actions->get_Item(1, &action)) || FAILED(action.As(&exec)) || FAILED(exec->get_Path(&raw)) || !raw) {
        // 任务在，但动作读不出来（被别的工具改过）：当作指向别处
        return AutostartState::OtherPath;
    }
    const std::wstring path = StripQuotes(raw);
    SysFreeString(raw);
    if (registered_path) {
        *registered_path = path;
    }
    return _wcsicmp(path.c_str(), exe_path) == 0 ? AutostartState::On : AutostartState::OtherPath;
}

bool SetAutostart(bool enabled, const wchar_t* exe_path) {
    ComScope com;
    if (!com.ok()) {
        return false;
    }
    const ComPtr<ITaskService> service = Connect();
    if (!service) {
        return false;
    }
    if (enabled) {
        const bool ok = Register(service.Get(), exe_path);
        log::Write(L"autostart: register %s (%s)", ok ? L"ok" : L"failed", exe_path);
        return ok;
    }

    ComPtr<ITaskFolder> folder;
    HRESULT hr = service->GetFolder(Bstr(names::kAutostartFolder), &folder);
    if (IsNotFound(hr)) {
        return true;  // 本来就没有
    }
    if (!Check(hr, L"GetFolder")) {
        return false;
    }
    hr = folder->DeleteTask(Bstr(names::kAutostartName), 0);
    if (!IsNotFound(hr) && !Check(hr, L"DeleteTask")) {
        return false;
    }
    // 文件夹空了就一并删掉，别在任务计划程序里留个空目录；不空（用户放了别的任务）就保留
    ComPtr<IRegisteredTaskCollection> rest;
    LONG count = 0;
    if (SUCCEEDED(folder->GetTasks(TASK_ENUM_HIDDEN, &rest)) && SUCCEEDED(rest->get_Count(&count)) && count == 0) {
        ComPtr<ITaskFolder> root;
        if (SUCCEEDED(service->GetFolder(Bstr(L"\\"), &root))) {
            root->DeleteFolder(Bstr(names::kAutostartFolder + 1), 0);
        }
    }
    log::Write(L"autostart: removed");
    return true;
}

}  // namespace pitot::app
