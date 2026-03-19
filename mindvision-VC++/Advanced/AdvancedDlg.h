// AdvancedDlg.h : header file
//
//BIG5 TRANS ALLOWED

#if !defined(AFX_ADVANCEDDLG_H__4B7A0F2F_62FA_4C67_AB8B_8DE471779F2C__INCLUDED_)
#define AFX_ADVANCEDDLG_H__4B7A0F2F_62FA_4C67_AB8B_8DE471779F2C__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000


#include "SdkCallTrace.h" // Log information for displaying SDK call functions in routine
/////////////////////////////////////////////////////////////////////////////
// CAdvancedDlg dialog

// reference camera SDK package, lib file path according to your development environment changes.
#include "CameraApi.h"
#ifdef _WIN64
#pragma comment (lib, "..\\MVCAMSDK_X64.lib")
#else
#pragma comment (lib, "..\\MVCAMSDK.lib")
#endif
/* Call camera's SDK interface log message in output routines */
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
class CAdvancedDlg : public CDialog
{
	// Construction
public:
	CAdvancedDlg(CWnd* pParent = NULL);	// standard constructor

	// Dialog Data
	//{{AFX_DATA(CAdvancedDlg)
	enum { IDD = IDD_ADVANCED_DIALOG_CN };
	CComboBox m_FrameSpeedList; // Frame rate selection list
	CComboBox m_cClrTmpList; // color temperature list control
	CSliderCtrl m_cSldAeTarget; // AE brightness target scrollbar
	CSliderCtrl m_cSldSaturation; // Saturation adjustment scrollbar
	CSliderCtrl m_cSldRedGain; // Red digital gain adjustment scroll bar
	CSliderCtrl m_cSldGreenGain; // green digital gain adjustment scrollbar
	CSliderCtrl m_cSldGamma; // gamma adjustment scroll bar
	CSliderCtrl m_cSldExposureLines; // frame exposure line adjustment scroll bar
	CSliderCtrl m_cSldSharppen; // sharpen adjustment scrollbar
	CSliderCtrl m_cSldContrast; // Contrast adjustment scrollbar
	CSliderCtrl m_cSldBlueGain; // blue digital gain adjustment scroll bar
	CSliderCtrl m_cSldAnalogGain; // analog gain adjustment scroll bar
	CComboBox m_cPresetLutList; // Default LUT list
	CComboBox m_cResolutionList; // resolution list
	CStatic m_cPreview; // Display image controls
	int m_iTriggerModeSel; // trigger selection mode
	int m_iSnapfileTypeSel; // capture file save format
	int m_iSnapshotResSel; // Scared resolution mode (which can be the same as the preview can also be set separately)
	int m_iWbModeSel; // white balance mode (divided into manual and automatic two)
	int m_iLutModeSel; // LUT setting mode
	int m_iExposureModeSel; // Exposure mode (divided into automatic and manual)
	int m_iAntiflickFreqSel; // frequency of anti-stroboscopic selection of automatic exposure (divided into two kinds of 50 and 60 Hz)
	int m_iCrossPositionX; // The X value of the center of the crosshair
	int m_iCrossPositionY; // The Y value of the center of the crosshair
	int m_iHardwareTriggerDelayUs; // Delay when hard trigger, unit us
	int m_iTriggerCount; // Touch mode, the number of frames triggered by a trigger signal
	int m_iClrTempSel; // The index of the currently selected color temperature in the list
	CString m_sSnapFilePath; // capture path to save the image
	BOOL m_bVflip; // Image vertical mirroring flag
	BOOL m_bHflip; // Image horizontal mirroring flag
	BOOL m_bAntiFlick; // Anti-strobing enable flag
	BOOL m_bNoiseReduce; // noise reduction enable flag
	BOOL m_bDisplayWbWindow; // white balance window display flag
	BOOL m_bDisplayAeWindow; // AE window display flag
	BOOL m_bDisplayCrosshair; // Crosshair display enable flag
	int m_iAutoSnapCycleMs; // automatic capture cycle, in milliseconds
	int m_iParamGroupSel; // current parameter group index number
	BOOL m_bSaveParamOnExit; // Save the parameter's enable flag when exiting the program
	BOOL m_bSaveParamOnSwitch; // Save parameter enable flag when switching parameter group
	UINT m_uCrosshairColor; // The color of the crosshair
	CSdkCallTrace m_DlgLog; // Information display window
	BOOL m_bNotOverlayOnSnap; // snapshot superimposed crosshair and other information
	tSdkCameraCapbility m_sCameraInfo; // camera characterization
	//}}AFX_DATA

	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CAdvancedDlg)
protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV support
	//}}AFX_VIRTUAL

	// Implementation
protected:
	HICON m_hIcon;

	void MySetDlgItemText(UINT uItemID, LPCSTR pFmt, ...);

	// Generated message map functions
	//{{AFX_MSG(CAdvancedDlg)
	virtual BOOL OnInitDialog();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	afx_msg void OnButtonRoiReset();
	afx_msg void OnSelchangeComboResolution();
	afx_msg void OnButtonSoftTrigOnce();
	afx_msg void OnButtonSnapshot();
	afx_msg void OnButtonAeWinSet();
	afx_msg void OnBtnWbOnce();
	afx_msg void OnBtnWbWinSet();
	afx_msg void OnButtonCustomResSnapshot();
	afx_msg void OnButtonBrowseSnapshot();
	afx_msg void OnRadioTriggerMode();
	afx_msg void OnChangeEdtTriggerCount();
	afx_msg void OnChangeEdtTriggerDelay();
	afx_msg void OnSetCrosshair();
	afx_msg void OnRadioExposureMode();
	afx_msg void OnChkAntiFlick();
	afx_msg void OnAntiFilckhz();
	afx_msg void OnCheckDisplayAeWin();
	afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg void OnCheckNoiseReduce();
	afx_msg void OnButtonLutLoadCustom();
	afx_msg void OnButtonEditLut();
	afx_msg void OnSelchangeComboPresetLut();
	afx_msg void OnRadioLutMode();
	afx_msg void OnRadioWbMode();

#ifdef _WIN64
	afx_msg void OnTimer(UINT_PTR nIDEvent); 
#else
	afx_msg void OnTimer(UINT nIDEvent);
#endif

	afx_msg void OnClose();
	afx_msg void OnButtonSaveParam();
	afx_msg void OnButtonLoadDefault();
	afx_msg void OnButtonLoadParamFromFile();
	afx_msg void OnRadioChangeParamGroup();
	afx_msg void OnCheckDisWbWin();
	afx_msg void OnButtonCustomCt();
	afx_msg void OnSelchangeComboCt();
	afx_msg void OnSelchangeComboFrameSpeed();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()


public:
	CameraHandle m_hCamera; // camera's device handle | the handle of the camera we use
	tSdkFrameHead m_sFrInfo; // Header information for saving the current image frame

	int m_iDispFrameNum; // is used to record the number of image frames currently displayed
	float m_fDispFps; // display frame rate
	float m_fCapFps; // capture frame rate
	tSdkFrameStatistic m_sFrameCount;
	tSdkFrameStatistic m_sFrameLast;
	int m_iTimeLast;
	BYTE *m_pFrameBuffer; // Buffer for converting raw image data to RGB (in the acquisition thread)
	BOOL m_bPause; // Whether to pause the image

	float m_fAnalogGainStep; // camera brightness gain adjustment step, different models of the camera, the value is not the same
	UINT m_uiMaxExpTime; // The longest time the camera is exposed. Units for the line, different types of cameras, the value is not the same

	UINT m_threadID; // image capture thread ID
	HANDLE m_hDispThread; // image grabbing thread handle
	BOOL m_bExit; // to notify the end of the image capture thread

	double m_fExpTime; // The current frame exposure time in us
	double m_fExpLineTime; // The current line exposure time in us
	INT m_iAnalogGain; // current signal analog gain value, the step value is m_fAnalogGainStep

	BYTE * m_pbImgBuffer; // used to save the original number of formats into RGB format image data (Snap mode)
	tSdkImageResolution m_tRoiResolution; // Custom resolution (in preview mode)
	tSdkImageResolution m_tRoiResolutionSnapshot; // Custom resolution (in preview mode)

	BOOL m_bMonoSensor; // TRUE said that the camera is a black and white camera
	// Camera initialization
	BOOL InitCamera ();

	// Initialize the control
	BOOL InitControls (tSdkCameraCapbility * pCameraInfo);

	// update the control's display
	VOID UpdateControls ();

	// update the exposure control's display
	VOID UpdateExposure ();

	// Check the file path is valid
	BOOL CheckPath ();

	// recursively create a full path
	BOOL mkdirEx(const char* lpPath);
	afx_msg void OnBnClickedAutoSnap();
	BOOL m_bAutoSnapshot;
	afx_msg void OnEnChangeEditExptext();
	afx_msg void OnEnChangeEditGaintext();
	afx_msg void OnBnClickedChkHfip();
	afx_msg void OnBnClickedChkVflip();
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_ADVANCEDDLG_H__4B7A0F2F_62FA_4C67_AB8B_8DE471779F2C__INCLUDED_)
