
// FirstStepDlg.cpp : 实现文件
//

#include "stdafx.h"
#include "FirstStep.h"
#include "FirstStepDlg.h"
#include "afxdialogex.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


extern int gLanguage;




CFirstStepDlg::CFirstStepDlg(CWnd* pParent /*=NULL*/)
	: CDialogEx(CFirstStepDlg::IDD, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void CFirstStepDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CFirstStepDlg, CDialogEx)
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_WM_DESTROY()
	ON_BN_CLICKED(IDC_BUTTON_SETTINGS, &CFirstStepDlg::OnBnClickedButtonSettings)
	ON_BN_CLICKED(IDC_BUTTON_PLAY, &CFirstStepDlg::OnBnClickedButtonPlay)
	ON_BN_CLICKED(IDC_BUTTON_STOP, &CFirstStepDlg::OnBnClickedButtonStop)
	ON_BN_CLICKED(IDC_BUTTON_SNAP, &CFirstStepDlg::OnBnClickedButtonSnap)
	ON_WM_TIMER()
END_MESSAGE_MAP()


// CFirstStepDlg 消息处理程序

BOOL CFirstStepDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	// Set the icon for this dialog. When the main application window is not a dialog box, the framework will automatically
	// do this
	SetIcon (m_hIcon, TRUE); // Set a large icon
	SetIcon (m_hIcon, FALSE); // Set a small icon

	CameraSdkInit(gLanguage);

	CameraSdkStatus status;
	m_Grabber = NULL;

	tSdkCameraDevInfo DevList[16];
	int nDev = ARRAYSIZE(DevList);
	status = CameraEnumerateDevice(DevList, &nDev);
	if (nDev == 1)
	{
		status = CameraGrabber_Create(&m_Grabber, &DevList[0]);
	}
	else if (nDev > 1)
	{
		status = CameraGrabber_CreateFromDevicePage(&m_Grabber);
	}
	
	if (status == CAMERA_STATUS_SUCCESS)
	{
		SetTimer(1, 1000, NULL);

		CameraGrabber_GetCameraHandle(m_Grabber, &m_hCamera);
		CameraGrabber_GetCameraDevInfo(m_Grabber, &m_DevInfo);

		CameraCreateSettingPage(m_hCamera, m_hWnd, m_DevInfo.acFriendlyName, NULL, NULL, 0);

		CameraGrabber_SetRGBCallback(m_Grabber, FrameRGBCallbackEntry, this);

		// black and white camera settings ISP output grayscale image
		// Color Camera ISP will output BGR24 image by default
		tSdkCameraCapbility cap;
		CameraGetCapability(m_hCamera, &cap);
		if (cap.sIspCapacity.bMonoSensor)
			CameraSetIspOutFormat(m_hCamera, CAMERA_MEDIA_TYPE_MONO8);

		CameraGrabber_SetHWnd(m_Grabber, GetDlgItem(IDC_DISP)->m_hWnd);
		CameraGrabber_StartLive(m_Grabber);
	}
	else
	{
		CString str;
		str.Format("打开相机失败！status=%d", status);
		MessageBox(str);
		EndDialog(0);
	}

	return TRUE;  // return TRUE unless the focus is set to the control
}

// If you add the minimize button to the dialog, you need the following code
// to draw the icon. For MFC applications that use the document / view model,
// This will be done automatically by the framework.

void CFirstStepDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); 

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialogEx::OnPaint();
	}
}

HCURSOR CFirstStepDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}

void CFirstStepDlg::OnDestroy()
{
	CDialogEx::OnDestroy();

	if (m_Grabber != NULL)
	{
		CameraGrabber_Destroy(m_Grabber);
		m_Grabber = NULL;
	}
}

void CFirstStepDlg::OnBnClickedButtonSettings()
{
	CameraShowSettingPage(m_hCamera, TRUE);
}

void CFirstStepDlg::OnBnClickedButtonPlay()
{
	CameraGrabber_StartLive(m_Grabber);
}

void CFirstStepDlg::OnBnClickedButtonStop()
{
	CameraGrabber_StopLive(m_Grabber);
}

void CFirstStepDlg::OnBnClickedButtonSnap()
{
	void* Image = NULL;
	if (CameraGrabber_SaveImage(m_Grabber, &Image, 2000) == CAMERA_STATUS_SUCCESS)
	{
		//Initialize the file name of the save file
		CString FileName;
		CString strTime = CTime::GetCurrentTime().Format(_T("%Y%m%d%H%M%S"));
		GetCurrentDirectory(MAX_PATH, FileName.GetBufferSetLength(MAX_PATH));
		FileName.ReleaseBuffer();
		FileName.AppendFormat("\\Snapshot%s.bmp", strTime);

		if (CameraImage_SaveAsBmp(Image, FileName) == CAMERA_STATUS_SUCCESS)
			MessageBox(FileName);
		else
			MessageBox("保存失败");
		CameraImage_Destroy(Image);
	}
	else
	{
		MessageBox("抓图超时");
	}
}

void CFirstStepDlg::OnTimer(UINT_PTR nIDEvent)
{
	tSdkGrabberStat Stat;
	CameraGrabber_GetStat(m_Grabber, &Stat);

	CString strText;
	strText.Format("Width:%u Height:%u DispFPS:%.02f CapFPS:%.02f", Stat.Width, Stat.Height, Stat.DispFps, Stat.CapFps);
	SetDlgItemText(IDC_STATIC_STAT, strText);

	CDialogEx::OnTimer(nIDEvent);
}

void CFirstStepDlg::FrameRGBCallback(void* Grabber, BYTE *pFrameBuffer, tSdkFrameHead* pFrameHead)
{
	// data processing callback

	// As the monochrome camera sets the ISP output grayscale image after the camera is turned on
	// So here pFrameBuffer = 8 bit grayscale data
	// otherwise BGR24 data will be output like a color camera

	// Color Camera ISP will output BGR24 image by default
	// pFrameBuffer = BGR24 data
}
