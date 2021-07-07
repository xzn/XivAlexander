#include "pch.h"
#include "DllMain.h"
#include "resource.h"
#include "XivAlexander/XivAlexander.h"

static Utils::Win32::LoadedModule s_hModule;
static Utils::Win32::ActivationContext s_hActivationContext;

const Utils::Win32::LoadedModule& Dll::Module() {
	return s_hModule;
}

const Utils::Win32::ActivationContext& Dll::ActivationContext() {
	return s_hActivationContext;
}

const char* XivAlexDll::LoaderActionToString(LoaderAction val) {
	switch (val) {
	case LoaderAction::Auto: return "auto";
	case LoaderAction::Ask: return "ask";
	case LoaderAction::Load: return "load";
	case LoaderAction::Unload: return "unload";
	case LoaderAction::Launcher: return "launcher";
	case LoaderAction::UpdateCheck: return "update-check";
	case LoaderAction::Internal_Update_Step2_ReplaceFiles: return "_internal_update_step2_replacefiles";
	case LoaderAction::Internal_Update_Step3_CleanupFiles: return "_internal_update_step3_cleanupfiles";
	case LoaderAction::Internal_Inject_HookEntryPoint: return "_internal_inject_hookentrypoint";
	case LoaderAction::Internal_Inject_LoadXivAlexanderImmediately: return "_internal_inject_loadxivalexanderimmediately";
	case LoaderAction::Internal_Cleanup_Handle: return "_internal_cleanup_handle";
	}
	return "<invalid>";
}

XivAlexDll::LoaderAction XivAlexDll::ParseLoaderAction(std::string val) {
	auto valw = Utils::FromUtf8(val);
	CharLowerW(&valw[0]);
	val = Utils::ToUtf8(valw);
	for (size_t i = 0; i < static_cast<size_t>(LoaderAction::Count_); ++i) {
		const auto compare = std::string(LoaderActionToString(static_cast<LoaderAction>(i)));
		auto equal = true;
		for (size_t j = 0; equal && j < val.length() && j < compare.length(); ++j) {
			equal = val[j] == compare[j];
		}
		if (equal)
			return static_cast<LoaderAction>(i);
	}
	throw std::runtime_error("invalid LoaderAction");
}

XIVALEXANDER_DLLEXPORT DWORD XivAlexDll::LaunchXivAlexLoaderWithTargetHandles(
		const std::vector<Utils::Win32::Process>& hSources,
		LoaderAction action,
		bool wait,
		const std::filesystem::path& launcherPath,
		const Utils::Win32::Process& waitFor) {
	const auto companion = launcherPath.empty() ? Dll::Module().PathOf().parent_path() / XivAlex::XivAlexLoaderNameW : launcherPath;
	if (!exists(companion))
		throw std::runtime_error(std::format("loader not found: {}", companion));
	
	Utils::Win32::Process companionProcess;
	{
		Utils::Win32::ProcessBuilder creator;
		creator.WithPath(companion)
			.WithArgument(true, L"")
			.WithAppendArgument(L"--handle-instead-of-pid")
			.WithAppendArgument(L"--action")
			.WithAppendArgument(LoaderActionToString(action));
		
		if (waitFor)
			creator.WithAppendArgument("--wait-process")
				.WithAppendArgument("{}", creator.Inherit(waitFor).Value());
		for (const auto& h : hSources)
			creator.WithAppendArgument("{}", creator.Inherit(h).Value());

		companionProcess = creator.Run().first;
	}
	
	if (!wait) 
		return 0;
	else {
		DWORD retCode = 0;
		companionProcess.Wait();
		if (!GetExitCodeProcess(companionProcess, &retCode))
			throw Utils::Win32::Error("GetExitCodeProcess");
		return retCode;
	}
}

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD fdwReason, LPVOID lpReserved) {	
	switch (fdwReason) {
		case DLL_PROCESS_ATTACH:
		{
			try {
				s_hModule.Attach(hInstance, Utils::Win32::LoadedModule::Null, false, "Instance attach failed <cannot happen>");
				s_hActivationContext = Utils::Win32::ActivationContext(ACTCTXW {
					.cbSize = sizeof ACTCTXW,
					.dwFlags = ACTCTX_FLAG_HMODULE_VALID | ACTCTX_FLAG_RESOURCE_NAME_VALID,
					.lpResourceName = MAKEINTRESOURCE(IDR_RT_MANIFEST_LATE_ACTIVATION),
					.hModule = Dll::Module(),
				});
				MH_Initialize();
			} catch (const std::exception& e) {
				Utils::Win32::DebugPrint(L"DllMain({:x}, DLL_PROCESS_ATTACH, {}) Error: {}",
					reinterpret_cast<size_t>(hInstance), reinterpret_cast<size_t>(lpReserved), e.what());
				return FALSE;
			}
			return TRUE;
		}

		case DLL_PROCESS_DETACH:
		{
			auto fail = false;
			if (const auto res = MH_Uninitialize(); res != MH_OK) {
				fail = true;
				Utils::Win32::DebugPrint(L"MH_Uninitialize error: {}", MH_StatusToString(res));
			}
			if (fail)
				TerminateProcess(GetCurrentProcess(), -1);
			return TRUE;
		}
	}
	return TRUE;
}

extern "C" __declspec(dllexport) int __stdcall XivAlexDll::DisableAllApps(void*) {
	EnableXivAlexander(0);
	EnableInjectOnCreateProcess(0);
	return 0;
}

extern "C" __declspec(dllexport) int __stdcall XivAlexDll::CallFreeLibrary(void*) {
	FreeLibraryAndExitThread(Dll::Module(), 0);
}
