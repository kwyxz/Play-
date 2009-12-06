#include "AppDef.h"
#include "AppConfig.h"
#include "PlayerWnd.h"
#include "PsfLoader.h"
#include "win32/Rect.h"
#include "win32/FileDialog.h"
#include "win32/AcceleratorTableGenerator.h"
#include "win32/MenuItem.h"
#include "FileInformationWindow.h"
#include "AboutWindow.h"
#include "string_cast.h"
//#include "string_cast_sjis.h"
#include "resource.h"
//#include "Utf8.h"
#include <afxres.h>
#include <functional>

#define CLSNAME			_T("PlayerWnd")
#define WNDSTYLE		(WS_CAPTION | WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_SYSMENU | WS_MINIMIZEBOX)
#define WNDSTYLEEX		(0)
#define WM_UPDATEVIS	(WM_USER + 1)

#define PREF_REVERB_ENABLED ("reverb.enabled")

using namespace Framework;
using namespace std;
using namespace std::tr1;

CPlayerWnd::CPlayerWnd(CPsfVm& virtualMachine) :
m_virtualMachine(virtualMachine),
m_frames(0),
m_ready(false),
m_accel(CreateAccelerators())
{
	CAppConfig::GetInstance().RegisterPreferenceBoolean(PREF_REVERB_ENABLED, true);

	if(!DoesWindowClassExist(CLSNAME))
	{
		RegisterClassEx(&Win32::CWindow::MakeWndClass(CLSNAME));
	}

	Create(WNDSTYLEEX, CLSNAME, APP_NAME, WNDSTYLE, Win32::CRect(0, 0, 470 * MAX_CORE, 580), NULL, NULL);
	SetClassPtr();

	SetMenu(LoadMenu(GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_MAINMENU)));

	SetTimer(m_hWnd, 0, 1000, NULL);

    {
        Win32::CRect clientRect(GetClientRect());
        unsigned int left = 0;
        unsigned int increment = clientRect.Right() / MAX_CORE;
        for(unsigned int i = 0; i < MAX_CORE; i++)
        {
            Win32::CRect rect(left, 0, left + increment, clientRect.Bottom()); 
	        m_regView[i] = new CSpuRegView(m_hWnd, rect, m_virtualMachine.GetSpuCore(i));
	        m_regView[i]->Show(SW_SHOW);
            left += increment;
        }
    }

	UpdateTitle();
	UpdateFromConfig();

	SetIcon(ICON_SMALL, LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_MAIN)));

	m_virtualMachine.OnNewFrame.connect(bind(&CPlayerWnd::OnNewFrame, this));
	
//	m_virtualMachine.SetSpuHandler(std::tr1::bind(&CPlayerWnd::CreateHandler, _T("SH_WaveOut.dll")));
	m_virtualMachine.SetSpuHandler(std::tr1::bind(&CPlayerWnd::CreateHandler, _T("SH_OpenAL.dll")));
}

CPlayerWnd::~CPlayerWnd()
{
	m_virtualMachine.Pause();
    for(unsigned int i = 0; i < MAX_CORE; i++)
    {
	    delete m_regView[i];
    }
}

long CPlayerWnd::OnWndProc(unsigned int msg, WPARAM wparam, LPARAM lparam)
{
	switch(msg)
	{
	case WM_UPDATEVIS:
        for(unsigned int i = 0; i < MAX_CORE; i++)
        {
		    m_regView[i]->Render();
		    m_regView[i]->Redraw();
        }
		break;
	}
	return TRUE;
}

void CPlayerWnd::Run()
{
    while(IsWindow())
    {
        MSG msg;
        GetMessage(&msg, 0, 0, 0);
        if(!TranslateAccelerator(m_hWnd, m_accel, &msg))
        {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
        }
    }
}

long CPlayerWnd::OnSize(unsigned int, unsigned int, unsigned int)
{
	if(m_regView != NULL)
	{
		//RECT rect = GetClientRect();
		//m_regView->SetSize(rect.right, rect.bottom);
	}
	return FALSE;
}

long CPlayerWnd::OnCommand(unsigned short id, unsigned short command, HWND hWndFrom)
{
	switch(id)
	{
	case ID_FILE_OPEN:
		{
			Win32::CFileDialog dialog;
			const TCHAR* filter = 
				_T("All Supported Files\0*.psf; *.minipsf; *.psf2; *.minipsf2\0")
				_T("PlayStation Sound Files (*.psf; *.minipsf)\0*.psf; *.minipsf\0")
				_T("PlayStation2 Sound Files (*.psf2; *.minipsf2)\0*.psf2; *.minipsf2\0");
			dialog.m_OFN.lpstrFilter = filter;
			if(dialog.SummonOpen(m_hWnd))
			{
				Load(string_cast<string>(dialog.GetPath()).c_str());
			}
		}
		break;
	case ID_FILE_PAUSE:
		PauseResume();
		break;
	case ID_FILE_FILEINFORMATION:
		ShowFileInformation();
		break;
	case ID_FILE_EXIT:
		Destroy();
		break;
	case ID_SETTINGS_ENABLEREVERB:
		EnableReverb();
		break;
	case ID_HELP_ABOUT:
		ShowAbout();
		break;
	}
	return FALSE;
}

long CPlayerWnd::OnTimer()
{
//	TCHAR fps[32];
//	_stprintf(fps, _T("%i"), m_frames);
//	SetText(fps);
//	m_frames = 0;
	return FALSE;
}

HACCEL CPlayerWnd::CreateAccelerators()
{
	Win32::CAcceleratorTableGenerator tableGenerator;
	tableGenerator.Insert(ID_FILE_PAUSE,			VK_F5,	FVIRTKEY);
	tableGenerator.Insert(ID_SETTINGS_ENABLEREVERB,	'R',	FVIRTKEY | FCONTROL);	
	return tableGenerator.Create();
}

CSoundHandler* CPlayerWnd::CreateHandler(const TCHAR* libraryPath)
{
	typedef CSoundHandler* (*HandlerFactoryFunction)();
	HMODULE module = LoadLibrary(libraryPath);
	HandlerFactoryFunction handlerFactory = reinterpret_cast<HandlerFactoryFunction>(GetProcAddress(module, "HandlerFactory"));
	CSoundHandler* result = handlerFactory();
	return result;
}

void CPlayerWnd::PauseResume()
{
	if(!m_ready) return;
	if(m_virtualMachine.GetStatus() == CVirtualMachine::PAUSED)
	{
		m_virtualMachine.Resume();
	}
	else
	{
		m_virtualMachine.Pause();
	}
}

void CPlayerWnd::ShowFileInformation()
{
	if(!m_ready) return;
	CFileInformationWindow fileInfo(m_hWnd, m_tags);
	fileInfo.DoModal();
}

void CPlayerWnd::EnableReverb()
{
	bool value = CAppConfig::GetInstance().GetPreferenceBoolean(PREF_REVERB_ENABLED);
	CAppConfig::GetInstance().SetPreferenceBoolean(PREF_REVERB_ENABLED, !value);
	UpdateReverbStatus();
	UpdateMenu();
}

void CPlayerWnd::ShowAbout()
{
	CAboutWindow about(m_hWnd);
	about.DoModal();
}

void CPlayerWnd::Load(const char* path)
{
	m_virtualMachine.Pause();
	m_virtualMachine.Reset();
	try
	{
		CPsfBase::TagMap tags;
		CPsfLoader::LoadPsf(m_virtualMachine, path, &tags);
		m_tags = CPsfTags(tags);
		try
		{
			float volumeAdjust = boost::lexical_cast<float>(m_tags.GetTagValue("volume"));
			m_virtualMachine.SetVolumeAdjust(volumeAdjust);
		}
		catch(...)
		{

		}
		m_virtualMachine.Resume();
		m_ready = true;
	}
	catch(const exception& except)
	{
		tstring errorString = _T("Couldn't load PSF file: \r\n\r\n");
		errorString += string_cast<tstring>(except.what());
		MessageBox(m_hWnd, errorString.c_str(), NULL, 16);
		m_ready = false;
	}
	UpdateTitle();
	UpdateMenu();
}

void CPlayerWnd::UpdateFromConfig()
{
	UpdateReverbStatus();
	UpdateMenu();
}

void CPlayerWnd::UpdateReverbStatus()
{
	m_virtualMachine.SetReverbEnabled(CAppConfig::GetInstance().GetPreferenceBoolean(PREF_REVERB_ENABLED));
}

void CPlayerWnd::UpdateTitle()
{
	const char* titleTag = "title";
	bool hasTitle = m_tags.HasTag("title");

	tstring title = APP_NAME;
	if(hasTitle)
	{
		title += _T(" - [ ");
		title += string_cast<tstring>(m_tags.GetTagValue(titleTag));
		title += _T(" ]");
	}

	SetText(title.c_str());
}

void CPlayerWnd::UpdateMenu()
{
	{
		Win32::CMenuItem reverbMenuItem = Win32::CMenuItem::FindById(GetMenu(m_hWnd), ID_SETTINGS_ENABLEREVERB);
		reverbMenuItem.Check(CAppConfig::GetInstance().GetPreferenceBoolean(PREF_REVERB_ENABLED));
	}
	{
		Win32::CMenuItem fileInfoMenuItem = Win32::CMenuItem::FindById(GetMenu(m_hWnd), ID_FILE_FILEINFORMATION);
		fileInfoMenuItem.Enable(m_ready);
	}
	{
		Win32::CMenuItem pauseMenuItem = Win32::CMenuItem::FindById(GetMenu(m_hWnd), ID_FILE_PAUSE);
		pauseMenuItem.Enable(m_ready);
	}
}

void CPlayerWnd::OnNewFrame()
{
	PostMessage(m_hWnd, WM_UPDATEVIS, 0, 0);
//	m_frames++;
}
