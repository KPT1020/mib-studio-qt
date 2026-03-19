// BasicDlg.cpp : implementation file
//
//BIG5 TRANS ALLOWED

#include "stdafx.h"
#include "Basic.h"

#include "BasicDlg.h"
#include "malloc.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

extern int gLanguage;
/////////////////////////////////////////////////////////////////////////////
// CAboutDlg dialog used for App About

/*Camera callback function for window configuration
hCamera: current camera handle
MSG: Message Type,
SHEET_MSG_LOAD_PARAM_DEFAULT = 0, // Load the default parameters of the button is clicked, load the default parameters to complete the trigger the message,
SHEET_MSG_LOAD_PARAM_GROUP = 1, // trigger the message after the switch parameter group is completed,
SHEET_MSG_LOAD_PARAM_FROMFILE = 2, // The Load Parameters button is clicked, which has been triggered after the camera parameters have been loaded from the file
SHEET_MSG_SAVE_PARAM_GROUP = 3 // Save parameters button is clicked, the message is triggered after the parameters are saved
For details, see the emSdkPropSheetMsg type in CameraDefine.h

uParam: parameters attached to the message, different messages, parameters have different meanings.
When MSG is SHEET_MSG_LOAD_PARAM_DEFAULT, uParam represents the index number loaded into the default parameter group, starting from 0, corresponding to the four groups of A, B, C and D
When MSG is SHEET_MSG_LOAD_PARAM_GROUP, uParam represents the index number of the parameter group after the switch, starting from 0, corresponding to the four groups of A, B, C and D
When MSG is SHEET_MSG_LOAD_PARAM_FROMFILE, uParam represents the index number of the parameter group covered by the parameter in the file, starting from 0, corresponding to the four groups of A, B, C and D, respectively
When MSG is SHEET_MSG_SAVE_PARAM_GROUP, uParam represents the index number of the currently saved parameter group, starting from 0 and corresponding to the four groups of A, B, C and D
*/
void _stdcall CameraSettingPageCallback(CameraHandle hCamera,UINT MSG,UINT uParam,PVOID pContext)
{
	CBasicDlg *pDialog = (CBasicDlg*)pContext;
	CString sMsg;

	if (MSG == SHEET_MSG_LOAD_PARAM_DEFAULT)
	{
		sMsg.Format("Parameter group[%d] was loaded to default!",uParam);
	}
	else if (MSG == SHEET_MSG_LOAD_PARAM_GROUP)
	{
		sMsg.Format("Parameter group[%d] was loaded!",uParam);
	}
	else if (MSG == SHEET_MSG_LOAD_PARAM_FROMFILE)
	{
		sMsg.Format("Parameter group[%d] was loaded from file!",uParam);
	}
	else if (MSG == SHEET_MSG_SAVE_PARAM_GROUP)
	{
		sMsg.Format("Parameter group[%d] was saved!",uParam);
	}
	else
	{
		return;//unknown message type
	}
	pDialog->m_DlgLog.AppendLog("CallBack: CameraSettingPageCallback");
	pDialog->m_DlgLog.AppendLog(sMsg);
	pDialog->m_DlgLog.AppendLog(" ");

}

/*
USE_CALLBACK_GRAB_IMAGE
If you need to use the callback function to obtain the image data, the anti-comment macro definition USE_CALLBACK_GRAB_IMAGE.
Our SDK supports both the callback function and the way the calling interface automatically fetch images. Both methods adopt a "zero-copy" mechanism to reduce system load to the greatest extent and improve program execution efficiency.
But take the initiative to crawl more flexible than the callback function, you can set the timeout wait, we recommend that you use uiDisplayThread in the way
*/
//#define USE_CALLBACK_GRAB_IMAGE 

#ifdef USE_CALLBACK_GRAB_IMAGE
/*Image capture callback function*/
void _stdcall GrabImageCallback(CameraHandle hCamera, BYTE *pFrameBuffer, tSdkFrameHead* pFrameHead,PVOID pContext)
{
	
	CameraSdkStatus status;
	CBasicDlg *pThis = (CBasicDlg*)pContext;
	
	// The original data obtained will be converted into RGB format data, and meanwhile through the ISP module, the image will be processed with noise reduction, edge enhancement and color correction.
	// Most of our company's cameras, the original data are Bayer format
	status = CameraImageProcess (pThis-> m_hCamera, pFrameBuffer, pThis-> m_pFrameBuffer, pFrameHead);

	// resolution has changed, then refresh the background
	if (pThis-> m_sFrInfo.iWidth! = pFrameHead-> iWidth || pThis-> m_sFrInfo.iHeight! = pFrameHead-> iHeight)
	{
		pThis-> m_sFrInfo.iWidth = pFrameHead-> iWidth;
		pThis-> m_sFrInfo.iHeight = pFrameHead-> iHeight;
		pThis-> InvalidateRect (NULL); // Erase the background when resizing the resolution.
	}
	
	if(status == CAMERA_STATUS_SUCCESS && !pThis->m_bPause)
    {
		// call the SDK display interface to display the image, you can also m_pFrameBuffer RGB data through other ways, such as directX, OpengGL, and so on.
		CameraImageOverlay (pThis-> m_hCamera, pThis-> m_pFrameBuffer, pFrameHead);
		CameraDisplayRGB24 (pThis-> m_hCamera, pThis-> m_pFrameBuffer, pFrameHead); // Here you can replace the user's own display function
        pThis->m_iDispFrameNum++;

		pThis->ProcessSnapRequest(pThis->m_pFrameBuffer, pFrameHead);
    }    
    
	memcpy(&pThis->m_sFrInfo,pFrameHead,sizeof(tSdkFrameHead));
	
}

#else 
/*Image capture thread, take the initiative to call the SDK interface function to get the image*/
UINT WINAPI uiDisplayThread(LPVOID lpParam)
{
	tSdkFrameHead 	sFrameInfo;
	CBasicDlg* 		pThis = (CBasicDlg*)lpParam;
	BYTE*			pbyBuffer;
	CameraSdkStatus status;
	

	while (!pThis->m_bExit)
    {   

		if(CameraGetImageBufferPriority(pThis->m_hCamera,&sFrameInfo,&pbyBuffer,1000,
			CAMERA_GET_IMAGE_PRIORITY_NEWEST) == CAMERA_STATUS_SUCCESS)
		{	
			// The original data obtained will be converted into RGB format data, and meanwhile through the ISP module, the image will be processed with noise reduction, edge enhancement and color correction.
			// Most of our company's cameras, the original data are Bayer format
			status = CameraImageProcess (pThis-> m_hCamera, pbyBuffer, pThis-> m_pFrameBuffer, & sFrameInfo); // Continuous mode

			// resolution has changed, then refresh the background
			if (pThis->m_sFrInfo.iWidth != sFrameInfo.iWidth || pThis->m_sFrInfo.iHeight != sFrameInfo.iHeight)
			{
				pThis->m_sFrInfo.iWidth = sFrameInfo.iWidth;
				pThis->m_sFrInfo.iHeight = sFrameInfo.iHeight;
				pThis->InvalidateRect(NULL);
			}
			
			if(status == CAMERA_STATUS_SUCCESS)
            {
            	//Call the SDK package display interface to display the image, you can also m_pFrameBuffer RGB data through other ways, such as directX, OpengGL, and so on.
				CameraImageOverlay(pThis->m_hCamera, pThis->m_pFrameBuffer, &sFrameInfo);
                CameraDisplayRGB24(pThis->m_hCamera, pThis->m_pFrameBuffer, &sFrameInfo);
                pThis->m_iDispFrameNum++;

				pThis->ProcessSnapRequest(pThis->m_pFrameBuffer, &sFrameInfo);
            }    
            
			// After successfully calling CameraGetImageBuffer, you have to call CameraReleaseImageBuffer to release the obtained buffer.
			// Otherwise call CameraGetImageBuffer, the program will be suspended, that other threads call CameraReleaseImageBuffer to release the buffer
            CameraReleaseImageBuffer(pThis->m_hCamera,pbyBuffer);
            
			memcpy(&pThis->m_sFrInfo,&sFrameInfo,sizeof(tSdkFrameHead));
		}
		
    }
	
	_endthreadex(0);
    return 0;
}
#endif

class CAboutDlg : public CDialog
{
public:
	CAboutDlg();

// Dialog Data
	//{{AFX_DATA(CAboutDlg)
	enum { IDD = IDD_ABOUTBOX };
	CString	m_sAboutInfo;
	//}}AFX_DATA

	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CAboutDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

	DECLARE_MESSAGE_MAP()
};


CAboutDlg::CAboutDlg() : CDialog(CAboutDlg::IDD)
{
	//{{AFX_DATA_INIT(CAboutDlg)
	m_sAboutInfo = _T("This example shows how to integrate the camera into your system in a very fast and easy way!");
	//}}AFX_DATA_INIT
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CAboutDlg)
	DDX_Text(pDX, IDC_EDIT_ABOUT, m_sAboutInfo);
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialog)
	//{{AFX_MSG_MAP(CAboutDlg)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CBasicDlg dialog

CBasicDlg::CBasicDlg(CWnd* pParent /*=NULL*/)
: CDialog(gLanguage == 0?IDD_BASIC_DIALOG:IDD_BASIC_DIALOG_CN, pParent)
{
	//{{AFX_DATA_INIT(CBasicDlg)
		// NOTE: the ClassWizard will add member initialization here
	//}}AFX_DATA_INIT
	// Note that LoadIcon does not require a subsequent DestroyIcon in Win32
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
	m_bPause = TRUE;
	m_hCamera = -1;
	m_iDispFrameNum = 0;
	m_bExit = FALSE;
	m_hDispThread = NULL;
	m_pFrameBuffer = NULL;
	m_SnapRequest = 0;
}

void CBasicDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CBasicDlg)
	DDX_Control(pDX, IDC_STATIC_PREVIEW, m_cPreview);
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CBasicDlg, CDialog)
	//{{AFX_MSG_MAP(CBasicDlg)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_BN_CLICKED(IDC_BUTTON_PREVIEW, OnButtonPreview)
	ON_BN_CLICKED(IDC_BUTTON_CAMERA_SETTINGS, OnButtonCameraSettings)
	ON_BN_CLICKED(IDC_BUTTON_SNAPSHOT, OnButtonSnapshot)
	ON_BN_CLICKED(IDC_BUTTON_SNAPSHOT_FAST, OnButtonSnapshotFast)
	ON_BN_CLICKED(IDC_BUTTON_ABOUT, OnButtonAbout)
	ON_WM_CLOSE()
	ON_WM_TIMER()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CBasicDlg message handlers

BOOL CBasicDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	// Add "About..." menu item to system menu.

	// IDM_ABOUTBOX must be in the system command range.
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != NULL)
	{
		CString strAboutMenu;
		strAboutMenu.LoadString(IDS_ABOUTBOX);
		if (!strAboutMenu.IsEmpty())
		{
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}

	// Set the icon for this dialog.  The framework does this automatically
	//  when the application's main window is not a dialog
	SetIcon(m_hIcon, TRUE);			// Set big icon
	SetIcon(m_hIcon, FALSE);		// Set small icon

	m_DlgLog.Create(IDD_DIALOG_LOG,this);

	do 
	{
		m_DlgLog.ShowWindow(SW_SHOW);
		
// 		m_DlgLog.AppendLog("Basic Demo start");
// 		m_DlgLog.AppendLog("LoadSdkApi was called to load SDK api from MVCAMSDK.dll");
// 		m_DlgLog.AppendLog("LoadSdkApi is open source in CameraApiLoad.h ");
// 		m_DlgLog.AppendLog("It shows how to load the api from MVCAMSDK.dll,");
// 		m_DlgLog.AppendLog("you can also use your own way to load MVCAMSDK.dll");


		//Init the SDK, 0: English version 1: Chinese version, the description of the camera and the SDK generated device configuration interface
		SDK_TRACE (CameraSdkInit (gLanguage), gLanguage? "Initialize SDK": "Init SDK");

		if (! InitCamera ())
		{
			break;
		}
		SetTimer (0,1000, NULL); // Use a timer to calculate the frame rate

		return TRUE;

	} while(0);
	
	//Did not find the camera or failed to initialize, exit the program
	EndDialog(0);
	return FALSE;  // return TRUE  unless you set the focus to a control
}

void CBasicDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	else
	{
		CDialog::OnSysCommand(nID, lParam);
	}
}

// If you add a minimize button to your dialog, you will need the code below
//  to draw the icon.  For MFC applications using the document/view model,
//  this is automatically done for you by the framework.

void CBasicDlg::OnPaint() 
{
	if (IsIconic())
	{
		CPaintDC dc(this); // device context for painting

		SendMessage(WM_ICONERASEBKGND, (WPARAM) dc.GetSafeHdc(), 0);

		// Center icon in client rectangle
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// Draw the icon
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialog::OnPaint();
	}
}

// The system calls this to obtain the cursor to display while the user drags
//  the minimized window.
HCURSOR CBasicDlg::OnQueryDragIcon()
{
	return (HCURSOR) m_hIcon;
}

void CBasicDlg::OnButtonPreview() 
{
	
	m_bPause = !m_bPause;
	if (gLanguage == 0)
	{
		GetDlgItem(IDC_BUTTON_PREVIEW)->SetWindowText(m_bPause?"Play":"Pause");
	}
	else
	{
		GetDlgItem(IDC_BUTTON_PREVIEW)->SetWindowText(m_bPause?"播放":"暂停");
	}
	
	if (m_bPause)
	{
		//Set the camera in pause mode
		SDK_TRACE(CameraPause(m_hCamera),gLanguage?"暂停相机工作":"Pause the camera");
	}
	else
	{
		//Set the camera in play mode
		SDK_TRACE(CameraPlay(m_hCamera),gLanguage?"开始预览":"start preview");
	}
}


void CBasicDlg::OnButtonCameraSettings() 
{
	
	//Show the settings window。
	SDK_TRACE(CameraShowSettingPage(m_hCamera,TRUE),gLanguage?"显示相机的属性配置窗口，该窗口由SDK内部生成":"show the camera config page");
}

void CBasicDlg::OnButtonSnapshot() //Snap a picture
{
	
	tSdkFrameHead FrameInfo;
	BYTE *pRawBuffer;
	BYTE *pRgbBuffer;
	CString sFileName;
	tSdkImageResolution sImageSize;
	CameraSdkStatus status;
	CString msg;
	memset(&sImageSize,0,sizeof(tSdkImageResolution));
	sImageSize.iIndex = 0xff;
	// CameraSetResolutionForSnap set the resolution when capturing, and the preview can be the same, can also be different.
	//sImageSize.iIndex = 0xff; sImageSize.iwidth and sImageSize.iHeight are set to 0, indicating the same resolution as when previewing.
	// If you want a separate resolution when capturing, please refer to our Snapshot routine. Or see the SDK manual for a detailed description of the CameraSetResolutionForSnap interface
	SDK_TRACE(CameraSetResolutionForSnap(m_hCamera,&sImageSize),gLanguage?"设置抓拍模式下的分辨率":"Set resolution for snapshot");

	// CameraSnapToBuffer capture a frame of image data to the buffer, the buffer by the SDK internal application, the successful call, you need
	// !!!!!! Note: CameraSnapToBuffer will switch the resolution to take pictures, which is slower. For real-time processing, use the CameraGetImageBuffer function to take a map or callback function.
	if((status = CameraSnapToBuffer(m_hCamera,&FrameInfo,&pRawBuffer,1000)) != CAMERA_STATUS_SUCCESS)
	{
		m_DlgLog.AppendLog("Function:[CameraSnapToBuffer] failed!");
		msg.Format("Error Code:%d. Get more information about the error in CameraDefine.h",status);
		m_DlgLog.AppendLog(msg);
		m_DlgLog.AppendLog(" ");
		MessageBox("Snapshot failed,is camera in pause mode?");
		return;
	}
	else
	{
		msg.Format("Function:[%s] SUCCESS!","CameraSnapToBuffer");
		m_DlgLog.AppendLog(msg);
		msg.Format("Description:%s.","Capture a image to the buffer in snapshot mode");
		m_DlgLog.AppendLog(msg);
		m_DlgLog.AppendLog(" ");
		
		// After successful capture, save to file
		CString msg;
		char sCurpath [MAX_PATH];
		CString strTime = CTime :: GetCurrentTime (). Format (_T ("% Y% m% d% H% M% S"));
		GetCurrentDirectory (MAX_PATH, sCurpath);
		sFileName.Format ("% s \\ Snapshot% s", sCurpath, strTime); // Initializes the file name of the save file

		// Apply a buffer, used to convert the raw data obtained to RGB data, and also get the image processing effect
		pRgbBuffer = (BYTE *)CameraAlignMalloc(FrameInfo.iWidth*FrameInfo.iHeight*4,16);
		//Process the raw data,and get the return image in RGB format
		SDK_TRACE(CameraImageProcess(m_hCamera,pRawBuffer,pRgbBuffer,&FrameInfo),
			gLanguage?"处理图像，并得到RGB格式的数据":"process the raw data to rgb data");
		
		 //Release the buffer which get from CameraSnapToBuffer or CameraGetImageBuffer
		SDK_TRACE(CameraReleaseImageBuffer(m_hCamera,pRawBuffer),
			gLanguage?"释放由CameraSnapToBuffer、CameraGetImageBuffer获得的图像缓冲区":"Release the buffer malloced by CameraSnapToBuffer or CameraGetImageBuffer");
													  
		// CameraSaveImage Save the image, here only demonstrates how to save the BMP image. If you need to save into other formats, such as JPG, PNG, RAW, etc.,
		// Please refer to our Snapshot routine. Or see the SDK manual for a detailed description of the CameraSaveImage interface
		if((status = CameraSaveImage(m_hCamera,sFileName.GetBuffer(1),pRgbBuffer,&FrameInfo,FILE_BMP,100)) != CAMERA_STATUS_SUCCESS)
		{
			m_DlgLog.AppendLog("Function:[CameraSaveImage] failed!");
			msg.Format("Error Code:%d. Get more information about the error in CameraDefine.h",status);
			m_DlgLog.AppendLog(msg);
			m_DlgLog.AppendLog(" ");
			msg.Format("Failed to save image to [%s] ,please check the path",sFileName);
			MessageBox(msg);
		}
		else
		{
			msg.Format("Function:[%s] SUCCESS!","CameraSaveImage");
			m_DlgLog.AppendLog(msg);
			msg.Format("Description:%s.","Save the image data in the buffer to the file");
			m_DlgLog.AppendLog(msg);
			m_DlgLog.AppendLog(" ");

			msg.Format("Snapshot one image to file:[%s.BMP]",sFileName);
			MessageBox(msg);
		}
		
		CameraAlignFree(pRgbBuffer);
		
	}
}

void CBasicDlg::OnButtonSnapshotFast() //Snap a picture
{
	InterlockedIncrement(&m_SnapRequest);
}

void CBasicDlg::ProcessSnapRequest(BYTE* pImageData, tSdkFrameHead *pImageHead)
{
	if (m_SnapRequest == 0)
		return;
	InterlockedDecrement(&m_SnapRequest);

	static ULONG s_Index = 0;
	CString strFileName;
	GetCurrentDirectory(MAX_PATH, strFileName.GetBufferSetLength(MAX_PATH));
	strFileName.ReleaseBuffer();
	strFileName.AppendFormat("\\Snapshot%u", s_Index++);

	CameraSaveImage(m_hCamera, strFileName.GetBuffer(), pImageData, pImageHead, FILE_BMP, 100);
}

void CBasicDlg::OnButtonAbout() 
{
	CAboutDlg dlg;
	dlg.DoModal();
}

BOOL CBasicDlg::InitCamera()
{
	tSdkCameraDevInfo sCameraList[10];
	INT iCameraNums;
	CameraSdkStatus status;
	CRect rect;
	tSdkCameraCapbility sCameraInfo;
	
	//Enumerate the device to get the device list
	//Enumerate camera
	iCameraNums = 10;//Before calling CameraEnumerateDevice, set iCameraNums = 10, which means read only up to 10 devices. If you need to enumerate more devices, change the size of the sCameraList array and the value of iCameraNums
	
	if (CameraEnumerateDevice(sCameraList,&iCameraNums) != CAMERA_STATUS_SUCCESS || iCameraNums == 0)
	{
		MessageBox("No camera was found!");
		return FALSE;
	}

	//In this example, we only assume that a camera is connected. Therefore, only the first camera is initialized. (-1, -1) means that the parameter saved before last exit is loaded, and the default parameter is loaded if this camera is used for the first time.
	//In this demo ,we just init the first camera.
	if ((status = CameraInit(&sCameraList[0],-1,-1,&m_hCamera)) != CAMERA_STATUS_SUCCESS)
	{
		CString msg;
		msg.Format("Failed to init the camera! Error code is %d",status);
		MessageBox(msg + "，Because:" + CameraGetErrorString(status));
		return FALSE;
	}
	

	//Get properties description for this camera.
	SDK_TRACE(CameraGetCapability(m_hCamera,&sCameraInfo),gLanguage?"获得该相机的特性描述":"Get the capability of the camera");

	m_pFrameBuffer = (BYTE *)CameraAlignMalloc(sCameraInfo.sResolutionRange.iWidthMax*sCameraInfo.sResolutionRange.iHeightMax*4,16);	

	ASSERT(m_pFrameBuffer);

	if (sCameraInfo.sIspCapacity.bMonoSensor)
		CameraSetIspOutFormat(m_hCamera, CAMERA_MEDIA_TYPE_MONO8);
	
	//Use SDK to display camera images.
	SDK_TRACE(CameraDisplayInit(m_hCamera,m_cPreview.GetSafeHwnd()),gLanguage?"初始化显示接口":"Init display module");
	m_cPreview.GetClientRect(&rect);
	//Set display window size
	SDK_TRACE(CameraSetDisplaySize(m_hCamera,rect.right - rect.left,rect.bottom - rect.top),gLanguage?"设置显示窗口大小":"Set display size");
	
	// Notify the SDK to build the properties page for this camera. The name of the window is the name of the camera. You can also implement the camera's property configuration page based on the SDK's interface,
	// However, we recommend that you use the automatic creation within the SDK to save you a lot of time in interface development.
	//Create the settings window for the camera
	SDK_TRACE(CameraCreateSettingPage(m_hCamera,GetSafeHwnd(),
								sCameraList[0].acFriendlyName,CameraSettingPageCallback,(PVOID)this,0)
								,gLanguage?"通知SDK内部建该相机的属性页面":"Create camera config page");
 
	#ifdef USE_CALLBACK_GRAB_IMAGE //If you want to use the callback function, define this macro USE_CALLBACK_GRAB_IMAGE
	//Set the callback for image capture
	SDK_TRACE(CameraSetCallbackFunction(m_hCamera,GrabImageCallback,(PVOID)this,NULL),gLanguage?"设置图像抓取的回调函数":"Set image grab call back function");
	#else
	m_hDispThread = (HANDLE)_beginthreadex(NULL, 0, &uiDisplayThread, this, 0,  &m_threadID);
	ASSERT (m_hDispThread); 
	SetThreadPriority(m_hDispThread,THREAD_PRIORITY_HIGHEST);
	#endif
	//Tell the camera begin to sendding image
	SDK_TRACE(CameraPlay(m_hCamera),gLanguage?"开始预览":"Start preview");
	m_bPause = FALSE;
	GetDlgItem(IDC_BUTTON_PREVIEW)->SetWindowText(gLanguage?"暂停":"Pause");
	return TRUE;
}


void CBasicDlg::OnClose() 
{
	// Anti-initialize the camera
	if (m_hCamera> 0)
	{
		if (NULL != m_hDispThread)
		{
			// Wait for the collection thread to finish
			m_bExit = TRUE;
			:: WaitForSingleObject (m_hDispThread, INFINITE);
			CloseHandle (m_hDispThread);
			m_hDispThread = NULL;
		}

		// Anti-initialize the camera.
		CameraUnInit(m_hCamera);
		m_hCamera = 0;
	}

    if (m_pFrameBuffer)
    {
        CameraAlignFree(m_pFrameBuffer);
        m_pFrameBuffer = NULL;
    }
	CDialog::OnClose();
}

void CBasicDlg::OnTimer(UINT_PTR nIDEvent)//Refresh image information in one second: resolution, display frame rate, acquisition frame rate
{
	CString strStatusText;
    int iTimeCurrnet = 0;
	static int iDispNum = 0;

   	//Get the camera from the SDK internal acquisition of the total number of frames, lost frames and so on.
    CameraGetFrameStatistic(m_hCamera, &m_sFrameCount);
    iTimeCurrnet = GetTickCount();

    if (0 != iTimeCurrnet-m_iTimeLast)
    {
        m_fCapFps = (float)((m_sFrameCount.iCapture - m_sFrameLast.iCapture)*1000.0)/(float)(iTimeCurrnet-m_iTimeLast);
        m_fDispFps = (float)((m_iDispFrameNum - iDispNum)*1000.0)/(float)(iTimeCurrnet-m_iTimeLast);
    }
    else
    {
		return;
    }        
	
    m_iTimeLast = iTimeCurrnet;

    //Update frame information
	if (gLanguage != 0)//chinese
	{
		strStatusText.Format("| 图像分辨率:%d*%d | 显示帧率:%4.2f FPS | 捕获帧率:%4.2f FPS |",
			m_sFrInfo.iWidth, m_sFrInfo.iHeight,
        m_fDispFps, m_fCapFps);
	}
	else//english
	{
		strStatusText.Format("| Resolution:%d*%d | Display rate:%4.2f FPS | Capture rate:%4.2f FPS |",
			m_sFrInfo.iWidth, m_sFrInfo.iHeight,
        m_fDispFps, m_fCapFps);
	}
	GetDlgItem(IDC_STATIC_INFORMATION)->SetWindowText(strStatusText);

    m_sFrameLast.iCapture = m_sFrameCount.iCapture;
    m_sFrameLast.iLost = m_sFrameCount.iLost;
    m_sFrameLast.iTotal = m_sFrameCount.iTotal;
    iDispNum = m_iDispFrameNum;
	
    CDialog::OnTimer(nIDEvent);
}


