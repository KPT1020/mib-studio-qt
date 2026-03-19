// BasicDlg.h : header file
//
//BIG5 TRANS ALLOWED

#if !defined(AFX_BASICDLG_H__DE07E1D0_D0B7_4FA5_A4F3_45499366E00E__INCLUDED_)
#define AFX_BASICDLG_H__DE07E1D0_D0B7_4FA5_A4F3_45499366E00E__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "SdkCallTrace.h"

#include "CameraApi.h"	 
#ifdef _WIN64
#pragma comment(lib, "..\\MVCAMSDK_X64.lib")
#else
#pragma comment(lib, "..\\MVCAMSDK.lib")
#endif
//Calling camera's SDK interface log information in output routines
#define SDK_TRACE(_FUNC_,TXT) \
{\
	CameraSdkStatus status;\
	CString msg;\
	CString FuncName;\
	FuncName = #_FUNC_;\
	FuncName = FuncName.Left(FuncName.FindOneOf("("));\
	\
	status = _FUNC_;\
	if (status != CAMERA_STATUS_SUCCESS)\
	{\
	msg.Format(gLanguage?"函数:[%s] 调用失败!":"Function:[%s] return error",FuncName);\
	m_DlgLog.AppendLog(msg);\
	msg.Format(gLanguage?"错误码:%d. 请参考CameraStatus.h中错误码的详细定义":"Error code:%d.refer to CameraStatus.h for more information",status);\
	m_DlgLog.AppendLog(msg);\
	}\
	else\
	{\
	msg.Format(gLanguage?"函数:[%s] 调用成功!":"Function:[%s] success",FuncName);\
	m_DlgLog.AppendLog(msg);\
	msg.Format(gLanguage?"功能:%s.":"Action:%s",TXT);\
	m_DlgLog.AppendLog(msg);\
	}\
	msg = "";\
	m_DlgLog.AppendLog(msg);\
}


/////////////////////////////////////////////////////////////////////////////
// CBasicDlg dialog

class CBasicDlg : public CDialog
{
	// Construction
public:
	CBasicDlg(CWnd* pParent = NULL);	// standard constructor
	CSdkCallTrace m_DlgLog;
	// Dialog Data
	//{{AFX_DATA(CBasicDlg)
	enum { IDD = IDD_BASIC_DIALOG_CN };
	// NOTE: the ClassWizard will add data members here
	//}}AFX_DATA

	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CBasicDlg)
protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV support
	//}}AFX_VIRTUAL

	// Implementation

protected:
	HICON m_hIcon;

	// Generated message map functions
	//{{AFX_MSG(CBaseDlg)
	virtual BOOL OnInitDialog();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	afx_msg void OnButtonPreview();
	afx_msg void OnButtonCameraSettings();
	afx_msg void OnButtonSnapshot();
	afx_msg void OnButtonSnapshotFast();
	afx_msg void OnButtonAbout();
	afx_msg void OnClose();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()

public:
	CStatic m_cPreview; // Window controls for displaying camera images | the control used to display the images.
	CameraHandle m_hCamera; // camera's device handle | the handle of the camera we use
	tSdkFrameHead m_sFrInfo; // Header information for saving the current image frame

	int m_iDispFrameNum; //is used to record the number of image frames currently displayed
	float m_fDispFps; //display frame rate
	float m_fCapFps; //capture frame rate
	tSdkFrameStatistic m_sFrameCount;
	tSdkFrameStatistic m_sFrameLast;
	int m_iTimeLast;

	BYTE * m_pFrameBuffer; // Buffer for converting raw image data to RGB
	BOOL m_bPause; //Whether to pause the image

	UINT m_threadID; //image capture thread ID
	HANDLE m_hDispThread; //image grabbing thread handle
	BOOL m_bExit; //to notify the end of the image capture thread
	LONG m_SnapRequest; // Screenshot request

	BOOL InitCamera();
	void ProcessSnapRequest(BYTE* pImageData, tSdkFrameHead *pImageHead);
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_BASICDLG_H__DE07E1D0_D0B7_4FA5_A4F3_45499366E00E__INCLUDED_)
