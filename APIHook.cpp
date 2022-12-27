#include "stdafx.h"
#include "APIHook.h"
#include "minhook\hook.hh"
#include "Windows.h"

#define API_H_TRY try{
#define API_H_CATCH }catch(...){ATLASSERT(0);}



///////////////////////////////////////////////////////////////////////////////////////////
//@@COM
///////////////////////////////////////////////////////////////////////////////////////////
//TypeDef
typedef HRESULT(WINAPI* ORG_CoCreateInstance)(
	_In_  REFCLSID  rclsid,
	_In_  LPUNKNOWN pUnkOuter,
	_In_  DWORD     dwClsContext,
	_In_  REFIID    riid,
	_Out_ LPVOID    *ppv
	);
static ORG_CoCreateInstance pORG_CoCreateInstance = NULL;

////////////////////////////////////////////////////////////////
//HookFunction
static HRESULT WINAPI Hook_CoCreateInstance(
	_In_  REFCLSID  rclsid,
	_In_  LPUNKNOWN pUnkOuter,
	_In_  DWORD     dwClsContext,
	_In_  REFIID    riid,
	_Out_ LPVOID    *ppv
)
{
	PROC_TIME(Hook_CoCreateInstance)
	API_H_TRY
	if (theApp.IsSGMode())
	{
		if (rclsid == CLSID_FileOpenDialog || rclsid == CLSID_FileSaveDialog)
		{
			::SetLastError(ERROR_ACCESS_DENIED);
			return REGDB_E_CLASSNOTREG;
		}
	}
	API_H_CATCH
	HRESULT hRet = {0};
	hRet = pORG_CoCreateInstance(
		rclsid,
		pUnkOuter,
		dwClsContext,
		riid,
		ppv
	);
	return hRet;
}


///////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////
//@@ComDlg32
///////////////////////////////////////////////////////////////////////////////////////////
//TypeDef
typedef BOOL(WINAPI* FuncGetSaveFileNameW)(LPOPENFILENAMEW lpofn);
static FuncGetSaveFileNameW	pORG_GetSaveFileNameW = NULL;

typedef BOOL(WINAPI* FuncGetOpenFileNameW)(LPOPENFILENAMEW lpofn);
static FuncGetSaveFileNameW	pORG_GetOpenFileNameW = NULL;

typedef PIDLIST_ABSOLUTE(WINAPI* FuncSHBrowseForFolderW)(LPBROWSEINFOW lpbi);
static FuncSHBrowseForFolderW	pORG_SHBrowseForFolderW = NULL;

////////////////////////////////////////////////////////////////
//HookFunction
static BOOL WINAPI Hook_GetSaveFileNameW(
	LPOPENFILENAMEW lpofn
)
{
	BOOL bRet = FALSE;
	try
	{
		//Download禁止
		if (theApp.m_AppSettings.IsEnableDownloadRestriction())
		{
			return FALSE;
		}

		if (!theApp.IsSGMode())
			return pORG_GetSaveFileNameW(lpofn);

		CString strPath;
		strPath = theApp.m_AppSettings.GetRootPath();
		if (strPath.IsEmpty())
			strPath = _T("B:\\");
		strPath = strPath.TrimRight('\\');
		strPath += _T("\\");

		//フック関数を無効
		lpofn->Flags &= ~OFN_ENABLEHOOK;
		//ダイアログテンプレート無効
		lpofn->Flags &= ~OFN_ENABLETEMPLATE;

		//Longファイル名を強制
		lpofn->Flags |= OFN_LONGNAMES;
		//ネットワークボタンを隠す
		lpofn->Flags |= OFN_NONETWORKBUTTON;

		//最近使ったファイルを追加しない
		lpofn->Flags |= OFN_DONTADDTORECENT;
		//プレースバーを無効
		lpofn->FlagsEx |= OFN_EX_NOPLACESBAR;

		//ファイルを上書きするかどうか確認するプロンプトを表示
		lpofn->Flags |= OFN_OVERWRITEPROMPT;

		CStringW strCaption(theApp.m_strThisAppName);
		CStringW strMsg;
		for (;;)
		{
			WCHAR szSelPath[MAX_PATH + 1] = {0};
			bRet = pORG_GetSaveFileNameW(lpofn);
			if (!bRet)
				return bRet;

			lstrcpynW(szSelPath, lpofn->lpstrFile, MAX_PATH);
			CStringW strSelPath(szSelPath);
			strSelPath.MakeUpper();
			if (strSelPath.IsEmpty())
				return bRet;

			CStringW strRoot(strPath);
			strRoot.MakeUpper();
			if (strSelPath.Find(strRoot) != 0)
			{
				strMsg.Format(L"%sドライブ以外は指定できません。\n\n保存する場所から%sを指定しなおしてください。\n\n選択された場所[%s]", strRoot, strRoot, szSelPath);
				::MessageBoxW(lpofn->hwndOwner, strMsg, strCaption, MB_OK | MB_ICONWARNING);
				continue;
			}

			CStringW strTSG_Upload = strRoot + L"UPLOAD\\";
			if (strSelPath.Find(strTSG_Upload) == 0)
			{
				strMsg.Format(L"アップロードフォルダー[%s]には保存できません。\n\n指定しなおしてください。\n\n選択された場所[%s]", strTSG_Upload, szSelPath);
				::MessageBoxW(lpofn->hwndOwner, strMsg, strCaption, MB_OK | MB_ICONWARNING);
				continue;
			}
			return bRet;
		}
	}
	catch (...)
	{
		ATLASSERT(0);
	}
	return bRet;
}

static BOOL WINAPI Hook_GetOpenFileNameW(
	LPOPENFILENAMEW lpofn
)
{
	BOOL bRet = FALSE;
	try
	{
		//呼び出しもとを確認、親の親がNULLだったらChronosの設定画面から
		HWND hWindowOwner = GetParent(lpofn->hwndOwner);
		HWND hWindowParent = {0};
		if (hWindowOwner)
			hWindowParent = GetParent(hWindowOwner);
		//hWindowParentがNULLの場合は、そのまま
		if (!hWindowParent)
		{
			bRet = pORG_GetOpenFileNameW(lpofn);
			return bRet;
		}

		CStringW strCaption(theApp.m_strThisAppName);
		CStringW strMsg;

		//Upload禁止
		if (theApp.m_AppSettings.IsEnableUploadRestriction())
		{
			strMsg = (L"ファイル アップロードは、システム管理者により制限されています。");
			if (hWindowParent)
				theApp.SB_MessageBox(hWindowParent, strMsg, NULL, MB_OK | MB_ICONWARNING, TRUE);
			else
				::MessageBoxW(lpofn->hwndOwner, strMsg, strCaption, MB_OK | MB_ICONWARNING);
			return FALSE;
		}

		if (theApp.m_AppSettings.IsAdvancedLogMode())
		{
			CString strLogmsg;
			strLogmsg = _T("Hook_GetOpenFileNameW");
			theApp.WriteDebugTraceDateTime(strLogmsg, DEBUG_LOG_TYPE_DE);
		}
		CString strPath;
		CString strRootPath;
		if (theApp.IsSGMode())
		{
			//UploadTabを使う場合は、B:\\Uploadにする
			if (theApp.m_AppSettings.IsShowUploadTab())
			{
				strRootPath = theApp.m_AppSettings.GetRootPath();
				if (strRootPath.IsEmpty())
					strRootPath = _T("B:\\");
				strRootPath += _T("UpLoad");
				if (!theApp.IsFolderExists(strRootPath))
					strRootPath = _T("B:\\");
			}
			//UploadTabを使わない場合は、O:\\にする
			else
			{
				strRootPath = theApp.m_AppSettings.GetUploadBasePath();
				if (strRootPath.IsEmpty())
					strRootPath = _T("B:\\");
			}
			strPath = strRootPath;

			//フック関数を無効
			lpofn->Flags &= ~OFN_ENABLEHOOK;
			//ダイアログテンプレート無効
			lpofn->Flags &= ~OFN_ENABLETEMPLATE;

			//Longファイル名を強制
			lpofn->Flags |= OFN_LONGNAMES;
			//ネットワークボタンを隠す
			lpofn->Flags |= OFN_NONETWORKBUTTON;

			//最近使ったファイルを追加しない
			lpofn->Flags |= OFN_DONTADDTORECENT;
			//プレースバーを無効
			lpofn->FlagsEx |= OFN_EX_NOPLACESBAR;

			//ファイルを上書きするかどうか確認するプロンプトを表示
			lpofn->Flags |= OFN_OVERWRITEPROMPT;
		}
		else
		{
			strPath = SBUtil::GetDownloadFolderPath();
		}
		strPath = strPath.TrimRight('\\');
		strPath += _T("\\");

		if (!theApp.m_strLastSelectUploadFolderPath.IsEmpty())
		{
			if (theApp.IsFolderExists(theApp.m_strLastSelectUploadFolderPath))
			{
				strPath = theApp.m_strLastSelectUploadFolderPath;
			}
		}

		lpofn->lpstrInitialDir = strPath.GetString();
		for (;;)
		{
			WCHAR szSelFolderPath[MAX_PATH + 1] = {0};
			bRet = pORG_GetOpenFileNameW(lpofn);
			if (!bRet)
				return bRet;

			lstrcpynW(szSelFolderPath, lpofn->lpstrFile, MAX_PATH);
			CStringW strSelPath(szSelFolderPath);
			strSelPath.MakeUpper();
			if (strSelPath.IsEmpty())
				return bRet;

			if (theApp.IsSGMode()) {
				CStringW strRoot(strRootPath);
				strRoot.MakeUpper();
				if (strSelPath.Find(strRoot) != 0)
				{
					strMsg.Format(L"アップロードフォルダー[%s]以外からはアップロードできません。\n\n指定しなおしてください。\n\n選択された場所[%s]", strRoot, szSelFolderPath);
					::MessageBoxW(lpofn->hwndOwner, strMsg, strCaption, MB_OK | MB_ICONWARNING);
					continue;
				}
			}

			PathRemoveFileSpec(szSelFolderPath);
			theApp.m_strLastSelectUploadFolderPath = szSelFolderPath;

			if (theApp.m_AppSettings.IsEnableLogging() && theApp.m_AppSettings.IsEnableUploadLogging())
			{
				WCHAR szSelPath[MAX_PATH + 1] = {0};
				lstrcpynW(szSelPath, lpofn->lpstrFile, MAX_PATH);
				CString strFileName;
				WCHAR* ptrFile = NULL;
				ptrFile = PathFindFileNameW(szSelPath);
				if (ptrFile)
				{
					strFileName = ptrFile;
				}
				theApp.SendLoggingMsg(LOG_UPLOAD, strFileName, lpofn->hwndOwner);
			}
			return bRet;
		}
	}
	catch (...)
	{
		ATLASSERT(0);
	}
	return bRet;
}
static PIDLIST_ABSOLUTE WINAPI Hook_SHBrowseForFolderW(
	LPBROWSEINFOW lpbi
)
{
	CStringW strCaption(theApp.m_strThisAppName);
	CStringW strMsg;
	strMsg.Format(L"フォルダーの参照機能は利用できません。");
	::MessageBoxW(lpbi->hwndOwner, strMsg, strCaption, MB_OK | MB_ICONWARNING);

	return NULL;
}

///////////////////////////////////////////////////////////////////////////////////////////
//@@APIHook////////////////////////////////////////////////////////////////////////////////////
APIHookC::APIHookC()
{
	m_bInitFlg = FALSE;
};

APIHookC::~APIHookC()
{
	if (m_bInitFlg)
	{
		m_bInitFlg = FALSE;
		MH_Uninitialize();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void APIHookC::DoHookComDlgAPI()
{
	PROC_TIME(DoHookComDlgAPI)

	LPVOID pTargetA = {0};
	LPVOID pTargetW = {0};
	HMODULE hModule = {0};

	if (!m_bInitFlg)
	{
		if (MH_Initialize() != MH_OK)
			return;
		m_bInitFlg = TRUE;
	}

	hModule = GetModuleHandleW(L"comdlg32.dll");

	if (!pORG_GetSaveFileNameW)
	{
		if (hModule)
			pTargetW = GetProcAddress(hModule, "GetSaveFileNameW");
		if (MH_CreateHookApiEx(
			L"comdlg32.dll", "GetSaveFileNameW", &Hook_GetSaveFileNameW, &pORG_GetSaveFileNameW) != MH_OK)
			return;

		if (pTargetW == NULL) return;
		if (MH_EnableHook(pTargetW) != MH_OK)
			return;
	}

	if (!pORG_GetOpenFileNameW)
	{
		if (hModule)
			pTargetW = GetProcAddress(hModule, "GetOpenFileNameW");
		if (MH_CreateHookApiEx(
			L"comdlg32.dll", "GetOpenFileNameW", &Hook_GetOpenFileNameW, &pORG_GetOpenFileNameW) != MH_OK)
			return;

		if (pTargetW == NULL) return;
		if (MH_EnableHook(pTargetW) != MH_OK)
			return;
	}

	////////////////////////////////////////////////////////////
	hModule = GetModuleHandleW(L"shell32.dll");
	if (!pORG_SHBrowseForFolderW)
	{
		if (hModule)
			pTargetW = GetProcAddress(hModule, "SHBrowseForFolderW");
		if (MH_CreateHookApiEx(
			L"shell32.dll", "SHBrowseForFolderW", &Hook_SHBrowseForFolderW, &pORG_SHBrowseForFolderW) != MH_OK)
			return;

		if (pTargetW == NULL) return;
		if (MH_EnableHook(pTargetW) != MH_OK)
			return;
	}
	if (!pORG_CoCreateInstance)
	{
		if (MH_CreateHookApiEx(
			L"ole32.dll", "CoCreateInstance", &Hook_CoCreateInstance, &pORG_CoCreateInstance) != MH_OK)
			return;

		if (MH_EnableHook(&CoCreateInstance) != MH_OK)
			return;
	}
}
