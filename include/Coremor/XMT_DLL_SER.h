#pragma once
#pragma once
//*****
#define NOPARITY            0
#define ODDPARITY           1
#define EVENPARITY          2
#define ONESTOPBIT          0
#define ONE5STOPBITS        1
#define TWOSTOPBITS         2
#define CBR_110             110
#define CBR_300             300
#define CBR_600             600
#define CBR_1200            1200
#define CBR_2400            2400
#define CBR_4800            4800
#define CBR_9600            9600
#define CBR_14400           14400
#define CBR_19200           19200
#define CBR_38400           38400
#define CBR_56000           56000
#define CBR_57600           57600
#define CBR_115200          115200
#define CBR_128000          128000
#define CBR_256000          256000
#define disTime_01 20

#ifdef DLL_XMT_USB_EXPORTS
#define DLL_XMT_SER_API extern "C" _declspec(dllexport)
#else
#define DLL_XMT_SER_API extern "C" _declspec(dllexport)
#endif

#pragma once
#ifndef __SOMEFILE_H__
#define __SOMEFILE_H__
//
//OVERLAPPED m_ov;//是一个包含了用于异步输入输出的信息的结构体 
//HANDLE hComm, g_hCom;//串口的句柄  
//HWND hRbox;
//COMSTAT comstat;//包含串口结构信息 
//bool DisplayHEX = false;
//unsigned char *tmp_command;//接收到的数据
//bool RxInbool;//1表示有数据输入需要接收数据 如果没有则不需要接收数据 该变量需要人工清零复位在 接收数据后需要人为设置为 0 待下一个数据再次使用

			  //内部处理函数
float CalData(unsigned char kk0, unsigned char kk1, unsigned char kk2, unsigned char kk3);//用来转化 正负数
double XMT_ReDo_pro(unsigned char comand_Arr[]); //根据不同命令来做测试
double Res_command_pro(unsigned char T_D_3, unsigned char T_D_4);//解包命令

#endif

unsigned char* DataAnla_Pro(double f, unsigned char kk[4]);
DLL_XMT_SER_API int EntryXMT(LPCWSTR comname, long int BaudRate, HWND rhbox); //初始化 串口 
DLL_XMT_SER_API int EntryXMT_labview(int m_com, int BaudRate);//1 com1 2 com2 ,1 9600 2 115200 20190723 加入使用labview控制方便

															  //设定串口同时打开串口 EntryXMT(_T("COM3"),9600,NULL);//打开串口3设置 波特率为9600 VS2010 

															  //CString strFileName; //使用VC++ 6.0
															  //strFileName = "COM5";//使用VC++ 6.0
															  // LPCWSTR lpcwStrCOM = strFileName.AllocSysString();//使用VC++ 6.0
															  //int i = EntryXMT(lpcwStrCOM,9600,NULL);//使用VC++ 6.0

DLL_XMT_SER_API bool WriteArr(unsigned char * m_szWriteBuffer,//发送数据
	unsigned char m_nToSend// 发送数据长度
	);
DWORD WINAPI ThreadSendMsg(LPVOID lpParameter);
DLL_XMT_SER_API int ReceiveArr(unsigned char RcBuffArr[], int ReadCharNum); //返回数据以及输出长度 读取的数据长度ReadCharNum

DLL_XMT_SER_API double Res_command_proP(unsigned char T_D_3, unsigned char T_D_4);//解包命令
DLL_XMT_SER_API int ReceiveArrP(unsigned char RcBuffArr[]); //返回数据以及输出长度 读取的数据长度ReadCharNum
															//读数发送读取命令


DLL_XMT_SER_API void XMT_COMMAND_ReadDataP(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Channel_Num
	);



DLL_XMT_SER_API  bool openport(LPCWSTR portname); //内部函数
DLL_XMT_SER_API  bool setupdcb(int rate_arg); //内部函数
											  //关闭串口-多个串口怎样调用以及关闭？
DLL_XMT_SER_API int OpenComWithBit(int com_I, int B_Bound_int);//B_Bound_int 1 9600 2 38400 3 57600 4 115200 
DLL_XMT_SER_API int OpenComConnectRS232(int nPortNr, int iBaudRate);// 
DLL_XMT_SER_API  bool CloseSer(); //关闭该串口
								  /****************设备控制块的设置*********************/ //参数 波特率rate_arg： 9600 
DLL_XMT_SER_API void ClearSer();//清空串口
DLL_XMT_SER_API bool setupdcb_BaudRate(int rate_arg); //设置波特率 9600
													  //返回串口性质

DLL_XMT_SER_API DCB ReSerDBC();

void XMT_ReadMultReal_Do(unsigned char T_D_3, unsigned char T_D_4,
	unsigned char *OpenOrCloseFlag_0,
	double *Data_0,
	unsigned char *OpenOrCloseFlag_1,
	double *Data_1,
	unsigned char *OpenOrCloseFlag_2,
	double *Data_2
	);
void XMT_ReadMultReal(unsigned char comand_Arr[],
	unsigned char *OpenOrCloseFlag_0,
	double *Data_0,
	unsigned char *OpenOrCloseFlag_1,
	double *Data_1,
	unsigned char *OpenOrCloseFlag_2,
	double *Data_2
	);//将采集的数组进行分包处理
unsigned char XMT_ReDo_pro_Unit(unsigned char comand_Arr[]); //用于做53读取 下位机 单位数据
unsigned char Res_command_pro_Unit(unsigned char T_D_3, unsigned char T_D_4);//解包命令
void XMT_ReDo_pro_Arr(unsigned char comand_Arr[], unsigned char arrRec[3]); //读取带返回数据值得

																			//发送数据转换 浮点数转化为DA(0-65535)之间转换
void ChangeDataToDa(unsigned char TmpDa[2], float TmpSendData, float MaxData, float MinData); //将浮点数 转化为两个[0] 代表高字节 先发送 [1]代表低字节后发送
DLL_XMT_SER_API void dis_Num100us(int tmpUs_100us);//100us的整数倍
DLL_XMT_SER_API void ArrDataSend(unsigned char address, unsigned char Channel_Num, double arr[], int ArrLong, unsigned char flagOpenOrClose, int tmpUs_100us);//发送数组 以及数组长度 flagOpenOrClose开闭环 设定 'C'表示闭环 'O'表示开环,发送间隔100微秒整数倍
DLL_XMT_SER_API double SendDataAndReadDataFormMcu(unsigned char Address, double SendData, int ChannelFlag, unsigned char OpenAndClose, int Time100Us);//发送直接返回 发送的数据
																																					  // Address 地址
																																					  // double SendData 发送的数据
																																					  // int ChannelFlag 通道 
																																					  // unsigned char OpenAndClose
																																					  //int Time100Us 返回读取时间 100微秒的整数倍

																																			  //单点类  0 1  
DLL_XMT_SER_API void XMT_COMMAND_SinglePoint(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Channel_Num,
	double VoltOrMove_Data
	); //
	   //do 多路单点类 2 3  
DLL_XMT_SER_API   unsigned char XMT_COMMAND_MultSinglePoint(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	double VoltOrMove_Data_0,
	double VoltOrMove_Data_1,
	double VoltOrMove_Data_2
	);
//清零命令 4
DLL_XMT_SER_API void 	XMT_COMMAND_SinglePoint_Clear(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4
	);
//读数据类 5 6 
DLL_XMT_SER_API double XMT_COMMAND_ReadData(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Channel_Num
	);
// 7 8 
DLL_XMT_SER_API void 	XMT_COMMAND_ReadData_TS(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Channel_Num,
	unsigned char TimerSet_ms
	);
// 9 10 
DLL_XMT_SER_API void 	XMT_COMMAND_ReadData_TS_MultChannle(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char TimerSet_ms
	);
//停止读取 11
DLL_XMT_SER_API void 	XMT_COMMAND_ReadData_Stop(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4
	);
//波形类
//单路高速  12 13 
DLL_XMT_SER_API void 	XMT_COMMAND_WaveSetHighSingle(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Channel_Num,
	unsigned char WaveType,
	double FengFengZhi,
	double PinLvHz,
	double Pianzhi
	);

//停止发送  14 
DLL_XMT_SER_API void 	XMT_COMMAND_WaveSetHighSingleStop(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4
	);
//多路标准速度模式 15 16 
DLL_XMT_SER_API void 	XMT_COMMAND_WaveSetMultWave(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Channel_Num,
	unsigned char WaveType,
	double FengFengZhi,
	double PinLvHz,
	double Pianzhi
	);

//停止发送 17
DLL_XMT_SER_API void 	XMT_COMMAND_WaveSetMultWaveStop(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Channel_Num
	);

//辅助协助类
//设置  18 20 22 
DLL_XMT_SER_API void 	XMT_COMMAND_Assist_SetFlag(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Channel_Num,
	unsigned char SetFlag
	);

//读取flag数据 19 21 23 
DLL_XMT_SER_API unsigned char XMT_COMMAND_Assist_ReadFlag(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Channel_Num
	);


//标定和配置类
//设定系统类  24 26 28 30 32 34 
DLL_XMT_SER_API void 	XMT_COMMAND_SetSystemInfo(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Channel_Num,
	double SystemInfo
	);
//读数据类 25 27  29
DLL_XMT_SER_API double XMT_COMMAND_ReadSystemInfo(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Channel_Num
	);
//设定高低限
//30 32 34 old
DLL_XMT_SER_API void 	XMT_COMMAND_SetSystemHL_Limit(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	double SystemInfo
	);

//读取系统高低限 31 33 35 
DLL_XMT_SER_API double   XMT_COMMAND_ReadSystemHL_Limit(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Channel_Num
	);
// 36 设置PID 启动/停止 
DLL_XMT_SER_API void 	XMT_COMMAND_SETPID_RorH(
	unsigned char address,
	unsigned char Command_B3,//36
	unsigned char Command_B4,//0
	unsigned char Channel_Num,//0,1,2
	unsigned char PIDSetFlag//启动 'R' 停止 'H'
	); //启动 'R' 停止 'H'
	   //37 设置PID参数
DLL_XMT_SER_API void SendArray_PID_Channel(
	int address,//地址码
	int Command_B3,//指令码
	int Command_B4,//指令码
	unsigned char Channel_Num,
	float PID_P,
	float PID_I,
	float PID_D
	);//发送数据
	  //38 读取 PID 参数 
DLL_XMT_SER_API void Read_PID_Channel(
	int address,//地址码
	int Command_B3,//指令码
	int Command_B4,//指令码
	unsigned char Channel_Num,
	float PID_Rc[3]
	); //发送数据
	   //38 读取 PID 参数 
DLL_XMT_SER_API void Read_PID_Channel(
	int address,//地址码
	int Command_B3,//指令码
	int Command_B4,//指令码
	unsigned char Channel_Num,
	float PID_Rc[3]
	); //发送数据
	   //46 设定下位机地址 该命令重复发送10次
DLL_XMT_SER_API void 	XMT_COMMAND_SetMCUAddress(
	unsigned char address,//0x00 这个是广播地址 固定的
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char SetAddress////预设地址
	);
//47 读去下位机地址
DLL_XMT_SER_API unsigned char 	XMT_COMMAND_ReadMCUAddress(
	unsigned char address,//0x00 这个是广播地址 固定的
	unsigned char Command_B3,
	unsigned char Command_B4
	);
//48
//实时读数据(由上位机指定每个通道发送电压或位移)
DLL_XMT_SER_API void 	XMT_COMMAND_ReadData_TS_UpDoPro(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char TimerSet_ms,
	unsigned char Flag_Channe_OpenOrClose
	);
//读取系统类 49
//实时读数据(由下位机根据开闭环状态确定每个通道电压或位移)
DLL_XMT_SER_API void 	XMT_COMMAND_ReadData_TS_DownDoPro(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char TimerSet_ms
	);

// 50 幅度校正
DLL_XMT_SER_API void XMT_COMMAND_CONTROL_PID(
	int address_ma,//地址码
	int bao_long,  //包长
	int zhilingma_B3,//指令码
	int zhilingma_B4,//指令码
	unsigned char channel_num,//通道数
	unsigned char FLAG_CLoseOrOpen//开始结束
	);
//51 读取多路位移或电压数据
DLL_XMT_SER_API void 	XMT_COMMAND_ReadMultChannelMoveOrVolt(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char *OpenOrCloseFlag_0,
	double *Data_0,
	unsigned char *OpenOrCloseFlag_1,
	double *Data_1,
	unsigned char *OpenOrCloseFlag_2,
	double *Data_2
	);
//52 读取电压位移限制百分比
DLL_XMT_SER_API float 	XMT_COMMAND_ReadSystem_VoltPer(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4
	);
//53
DLL_XMT_SER_API unsigned char 	XMT_COMMAND_ReadSystem_Unit(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4
	);
//54 读取下位机波速启停速度
DLL_XMT_SER_API unsigned char  XMT_COMMAND_ReadWaveBeginAndStopSpeed(
	int address_ma,//地址码
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char channel_num // 通道数 0 1 2 
	);// 'H'代表高速  'L'代表低速 
	  //55上位机设置某一路波形启动停止速度（启动停止速度一致）
DLL_XMT_SER_API void XMT_COMMAND_SetWaveBeginAndStopSpeed(
	int address_ma,//地址码
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char channel_num, // 通道数 0 1 2 
	unsigned char WaveBeginAndStopFlag // 'H'代表高速  'L'代表低速 
	);

//56 上位机设置下位机单位
DLL_XMT_SER_API void XMT_COMMAND_SetMCUMardOrUm(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char MCUMardOrUm //0 角度 1 位移
	);
//57 上位机设置下位机机型
DLL_XMT_SER_API void XMT_COMMAND_SetMCUE09orOther(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char MCUDoFlag //0 E709 1 E517
	);
//58 设定下位机电压位移限制百分比
DLL_XMT_SER_API void XMT_COMMAND_SetMCUVoltOrUmPP(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	float tmpData //0到1 的小数
	);

// 59 
DLL_XMT_SER_API  void  XMT_COMMAND_ReadMCU_PIDFlag(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char ChannelFlag[3]//'Y'有效 'N'表示无效  返回数据值
	);
// 61 62 在多台机器 串口422相连接时候使用
// 63 
//通过usb通信端口设置下位机串口通信波特率
DLL_XMT_SER_API void  XMT_COMMAND_SetMCUComBit(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char ComBitFlag
	);
/* //设置下位机波特率
‘A’9600 ‘B’19200  ‘C’38400 ‘D’57600
‘E’76800‘F’115200 ‘G’128000‘H’230400
‘I’256000
//20170509
*/
// 64  AVR专用开启jtag
DLL_XMT_SER_API void  XMT_COMMAND_SetMCUJtag(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char AVRFlag //[5]开启AVR Jtag功能 ‘S’
	);
//65 从任意界面跳转到采集界面
DLL_XMT_SER_API void XMT_COMMAND_LetMCUToReadData(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4
	);

//相位角模式输出 66 67 
DLL_XMT_SER_API void XMT_COMMAND_WaveSetMultWaveXwj(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Channel_Num,
	unsigned char WaveType,
	double FengFengZhi,
	double PinLvHz,
	double Pianzhi,
	double Xwjiao
	);
//Mult数据 68 
//[5]通道数
//[6]‘S’启动当前路波形 ‘T’停止当前路波形
DLL_XMT_SER_API void XMT_COMMAND_XWJ_ChannelDoOrStop(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Channel_Num,// 0 1 2 
	unsigned char FlagMult //‘S’启动当前路波形 ‘T’停止当前路波形
	);

//Mult数据 69 ‘S’三路同步启动 'T'三路同时停止
DLL_XMT_SER_API unsigned char XMT_COMMAND_Assist_Flag(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char FlagMult
	);

//70 单点步进存储
DLL_XMT_SER_API unsigned char XMT_COMMAND_SaveDataArrToMCU(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Channle_flag,
	unsigned char Flag_AheadOrLeg,//0表示前48点 1表示后48点
	float ArrData[],
	unsigned char LongArrData,//发送的数据点数0到48,
	float MaxData,//最大数据值 如果开环 一般为150或120
	float MinData//最小数据值 根据实际需要来做决定
	);

//71 设定发送时间
//设定下位发送单点的时间  

//0.2 为0.2毫秒 
//0.1 发送不出来
///如果用40个点来代替一个正弦波，
///0.2毫秒40点为250赫兹
///0.4毫秒40点为84赫兹
///0.3毫秒40点为125赫兹
///1、2、4等毫秒级别准确。
////1秒到5秒准确//
///多了得秒数 会有点误差 没有详细测量。20170814-周一

DLL_XMT_SER_API void 	XMT_COMMAND_SetMCUSendDataTimer(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Channel_Num,
	float SendDataTimer//0.1毫秒-9999毫秒之间
	);


// 72 发送预先设定好的程序 启动/停止 
DLL_XMT_SER_API void 	XMT_COMMAND_SetMCU_BeginSend(
	unsigned char address,
	unsigned char Command_B3,//36
	unsigned char Command_B4,//0
	unsigned char Channel_Num,//0,1,2
	unsigned char RunFlag//'S'开始    'T' 停止   'P'暂停 
	);
// 73 设置开机DA初始值
DLL_XMT_SER_API void XMT_COMMAND_SetMCU_FlagDa(
	unsigned char address,
	unsigned char Command_B3,//36
	unsigned char Command_B4,//0
	unsigned char Channel_Num,//0,1,2
	unsigned char DaFlag,//'Z' 设定为0点参数 'M'设定最大值参数 
	float FlagForDa//[7][8][9][10] 参数数值浮点数
	);
// 74 设定开机DA初始值
DLL_XMT_SER_API void 	XMT_COMMAND_SetMCU_FlagVolt(
	unsigned char address,
	unsigned char Command_B3,//36
	unsigned char Command_B4,//0
	unsigned char Channel_Num,//0,1,2
	float FlagForVolt//[6][7][8][9] DA输出电压百分比 DA初始值最大值百分比为0-1小数，0代表输出0，1代表输出最大电压值
	);

// 75 AD采集调零调放大
DLL_XMT_SER_API void 	XMT_COMMAND_SetMCU_FlagAD(
	unsigned char address,
	unsigned char Command_B3,//36
	unsigned char Command_B4,//0
	unsigned char Channel_Num,//0,1,2
	unsigned char FlagAD,//[6]'Z'开始调零 'M'开始调放大
	unsigned char FlagCloseOrOpen //[7]'C'闭环 'O'开环
	);

// 76 读取下位机 电压或位移命令
DLL_XMT_SER_API void SendArray_ReadDataChannel_AllVolt(int address_ma,//地址码
	int bao_long,  //包长
	int zhilingma_B3,//指令码
	int zhilingma_B4,
	unsigned char DisTimer);//指令码	

							// 76  组合命令读取返回特定8路命令 内部使用8路AD
DLL_XMT_SER_API void SendKeilth(void);//测试命令
									  //77 命令 判断是否连接正常   
DLL_XMT_SER_API unsigned char CheckConnection(int Now_control_mcu_address,
	int bao_long,
	int Command_B3,//指令码
	int Command_B4,//指令码
	int WaitTime);//读取是否连接正常 如果发挥‘K'表示连接正常//读取是否连接正常 如果发挥‘K'表示连接正常


short CalData_8(unsigned char tmp_A, unsigned char tmp_B); //数据转换

void DoData(unsigned char tmp_arr[], unsigned char ArrLong, float tmpData_Arr[8]); // tmp_arr[] 译码数组 数组长度 返回的译码后的数组

DLL_XMT_SER_API void SendArray_ReadDataChannel_AllVoltAndRead(int address_ma,//地址码
	int bao_long,  //包长
	int Command_B3,//指令码
	int Command_B4,
	unsigned char DisTimer,
	float TmpF[8]);//指令码



unsigned char XMT_ReDo_proNew(unsigned char comand_Arr[]); //根据不同命令来做测试
unsigned char Res_command_proNew(unsigned char T_D_3, unsigned char T_D_4);

//B3=100 B4 =0
//标定下位机类型 //0 1 2 3 4 5 6 7代表E17 E18 E72 E73 E70 E53D E709

//20180629 验证情况
//B3=0 B4 =57 标定下位机型号
//标定下位机MCU类型
//0 E70.S3 1 E18 2 E53 3 E18 24bit 4E51.D12S //20180629

DLL_XMT_SER_API void  XMT_COMMAND_SetMCUNum(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char FlagMCUFlag
	);//0 1 2 3 4 5 6 7代表E17 E18 E72 E73 E70 E53D E709
	  //B3=100 B4 =0  78
	  //读取下位机类型 //0 1 2 3 4 5 6 7代表E17 E18 E72 E73 E70 E53D E709 //20180820 E63 返回 6 
DLL_XMT_SER_API unsigned char XMT_COMMAND_ReadMCUNum(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4
	);

// 79 标定电源信息 比较详细的信息
DLL_XMT_SER_API void  XMT_COMMAND_SetPowerConfig(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Flag_SheBeiNum_1,//CUP型号 ARM DSP AVR 0 1 2 3 
	unsigned char Flag_SheBeiNum_2,//驱动器通道数;  1 2 3 4 5
	unsigned char Flag_SheBeiNum_3,//下位机版本号设定 格式V2.0.2 （不可设定 下位机不处理） 
	unsigned char Flag_SheBeiNum_4,//
	unsigned char Flag_SheBeiNum_5,// 
	unsigned char Flag_SheBeiNum_6,//通信端口信息  网口/485/USB/422/RS232，0001 1111 代表全支持  ‘X' 补位
	unsigned char Flag_SheBeiNum_7,//'X'
	unsigned char Flag_SheBeiNum_8,//是否恒压 Y是 N否
	unsigned char Flag_SheBeiNum_9,//全桥 特殊全桥 半桥  0 1 2 3 
	unsigned char Flag_SheBeiNum_10,//模拟输入低 模拟输入高 -3.3V-到+3.3V输入标定为33 33 一号 低位
	unsigned char Flag_SheBeiNum_11,//模拟输入低 模拟输入高 -3.3V-到+3.3V输入标定为33 33 一号 高位
	unsigned char Flag_SheBeiNum_12,//二号 ‘X'补位
	unsigned char Flag_SheBeiNum_13,//二号 ‘X'补位
	unsigned char Flag_SheBeiNum_14,//三号 ‘X'补位
	unsigned char Flag_SheBeiNum_15,//三号 ‘X'补位
	unsigned char Flag_SheBeiNum_16,//传感器类型 R C L 
	unsigned char Flag_SheBeiNum_17,//数字带宽 1K 2K 10K 30K
	unsigned char Flag_SheBeiNum_18,//DA分辨率 发送实际分辨率数字  8 16 18 20 24
	unsigned char Flag_SheBeiNum_19,//AD分辨率 发送实际分辨率 8 16 18 20 24 
	unsigned char commandFlagArr[32],// 0 到 255 条明令
	unsigned char Flag_SheBeiNum_20,//'X' 补位
	unsigned char Flag_SheBeiNum_21,//'X' 补位
	unsigned char Flag_SheBeiNum_22,//'X' 补位
	unsigned char Flag_SheBeiNum_23//'X' 补位
	);

// B3 = 80,B4 = 0; //20180629 读取电源相关信息
DLL_XMT_SER_API void XMT_COMMAND_ReadPowerConfig(char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char RcArr[61]);

//unsigned char commandFlagArr[32],// 0 到 255 条明令

//B3=81 B4 =21  81 号命令 标定下位机配套台子信息
//标定台子类型 //例如 XP 611 P93 N12 三大类

DLL_XMT_SER_API void  XMT_COMMAND_SetMoveMNum(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Flag_SheBeiNum_1,//X,N,P
	unsigned char Flag_SheBeiNum_2,//P
	unsigned char Flag_SheBeiNum_3,
	unsigned char Flag_SheBeiNum_4, // 数字 0 到 255 
	unsigned char Flag_SheBeiNum_5, // 数字 0 到 255  两组数字的 和 例如 XP 611 P93 N12 三大类
	unsigned char Flag_SheBeiNum_6, //设备号 通道数 1 2 3 4 5 6通道
	unsigned char Flag_SheBeiNum_lei,//设备号 Z线运动 B摆台类 综合类型
	unsigned char Flag_SheBeiNum_HengYa,
	unsigned char Flag_YingPianJi//电阻 电容 电感  0 1 2 
	);
/*
unsigned char Flag_SheBeiNum_1;//设备号 型号  X 33
unsigned char Flag_SheBeiNum_2;//设备号 型号  P 33
unsigned char Flag_SheBeiNum_3;//设备号 数字 0 到 255 例如 XP 611 P93 N12 三大类
unsigned char Flag_SheBeiNum_4;//设备号 通道数 1 2 3 4 5 6通道
unsigned char Flag_SheBeiNum_lei;//设备号 Z线运动 B摆台类 综合类型
unsigned char Flag_SheBeiNum_HengYa;//设备号 是否恒压 1 带恒压　0 代表非恒压
unsigned char Flag_YingPianJi;//电阻 电容 电感  0 1 2
*/


//B3=100 B4 =22
//标定台子类型 //例如 XP 611 P93 N12 三大类
//unsigned char Flag_SheBeiNum_1,//X,N,P
//unsigned char Flag_SheBeiNum_2,//P
//unsigned char Flag_SheBeiNum_3, // 数字 0 到 255 
//unsigned char Flag_SheBeiNum_4, // 数字 0 到 255  两组数字的 和 例如 XP 611 P93 N12 三大类
//unsigned char Flag_SheBeiNum_5, //设备号 通道数 1 2 3 4 5 6通道
//unsigned char Flag_SheBeiNum_lei,//设备号 Z线运动 B摆台类 综合类型
//unsigned char Flag_SheBeiNum_HengYa,
//unsigned char Flag_YingPianJi//电阻 电容 电感  0 1 2 


// B3 = 84，83,B4 = 0; //4路实时读取
DLL_XMT_SER_API void XMT_COMMAND_ReadDataFourChannel_DisTimer(int address_ma,//地址码
	int Command_B3,//指令码
	int Command_B4,//指令码	
	unsigned char DisTimer);

//相位角模式输出 T 91 92
DLL_XMT_SER_API void XMT_COMMAND_WaveSetMultWaveXwj_T(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Channel_Num,
	unsigned char WaveType,
	double FengFengZhi,
	double PinLvHz,
	double Pianzhi,
	double Xwjiao,
	double Zhouqi
	);
//Mult数据 93 
//[6]‘S’启动当前路波形 ‘T’停止当前路波形
DLL_XMT_SER_API void XMT_COMMAND_XWJ_ChannelDoOrStop_T(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char FlagMult //‘S’启动当前路波形 ‘T’停止当前路波形
	);

//多路单点类 94 95  
DLL_XMT_SER_API unsigned char  XMT_COMMAND_MultSinglePointT(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	double VoltOrMove_Data_0,
	double VoltOrMove_Data_1,
	double VoltOrMove_Data_2,
	double VoltOrMove_Data_3
	);


//98 号命令 强制设定当前各通道波形状态作为开机默认波形
DLL_XMT_SER_API void XMT_COMMAND_MakeWaveBeginSet_Ser(
	unsigned char address_ma,
	unsigned char zhilingma_B3,
	unsigned char zhilingma_B4,
	unsigned char SendFlag
	); //SendFlag 1 默认波形功能 0表示开机默认 开启开机发送波形功能


DLL_XMT_SER_API void  XMT_COMMAND_ReadMoveMNum(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char ReadArrTmp[9]//读取返回数据
	);
//unsigned char address, 地址数据
//unsigned char Channel_Num, 对应通道号
//unsigned char flagOpenOrClose,开闭环数据
//double Point_A,//起始点数据
//double Point_B,//终止点数据
//int ArrLong,//数据的间隔数
//int tmpUs_100us//点与点之间的时间间隔


// 101  自动校准命令 
DLL_XMT_SER_API void XMT_COMMAND_Mudify_MorZ(
	int address,//地址码
	int Command_B3,//指令码
	int Command_B4,//指令码
	unsigned char ModFlag//'Z'设定0点参数 ‘M'设定最大值参数
	);

//113  读取4路电压数据值 下位机保持同样的开环状态
//114  读取4路位数移据值 下位机保持同样的开环状态
	DLL_XMT_SER_API void 	XMT_COMMAND_ReadMultChannelVolt(
		unsigned char address,
		unsigned char Command_B3,
		unsigned char Command_B4,
		double *OpenOrCloseData_0, //开闭环数据 
		double *OpenOrCloseData_1, //开闭环数据
		double *OpenOrCloseData_2, //开闭环数据
		double *OpenOrCloseData_3  //开闭环数据
		);

//115 切换中英文界面  ShowFlag 0 中位 1 英文
DLL_XMT_SER_API void 	XMT_COMMAND_ChangeShow(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char ShowFlag
	);

//B3=118 B4 =0 E80 定制摆镜画圆以及直线
DLL_XMT_SER_API void  XMT_COMMAND_LineABAndWRound(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	double Apoint_1,//单位毫弧度
	double Apoint_2,//单位毫弧度
	double Bpoint_1,//单位毫弧度
	double Bpoint_2,//单位毫弧度
	int DisTime,//单位微秒
	double R,//
	double PHz,//频率,
	double RInt//画圆个数,
	);

//119
//停止 118划线过程
DLL_XMT_SER_API void XMT_COMMAND_LineABAndWRound_Stop(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4
	);


//标定IP地址 120 
DLL_XMT_SER_API void XMT_COMMAND_Net_SetPI(
	unsigned char address,
	int Command_B3,//指令码
	int Command_B4,//指令码
	unsigned char MacArr[6],//物理地址(MAC) 
	unsigned char MacIP[4],//驱动器IP地址
	unsigned char MacIPzw[4],//驱动器子网掩码地址
	unsigned char MacIPwg[4],//驱动器网关IP地址 
	unsigned char MacIPDNS[4],//DNS地址地址
	unsigned char MacUDPPort[2]//UDP端口号
	);

//读取IP地址 121 读取驱动器网关
DLL_XMT_SER_API void XMT_COMMAND_Net_ReadPI(
	unsigned char address,
	int Command_B3,//指令码
	int Command_B4,//指令码
	unsigned char MacArr[6],//物理地址(MAC) 
	unsigned char MacIP[4],//驱动器IP地址
	unsigned char MacIPzw[4],//驱动器子网掩码地址
	unsigned char MacIPwg[4],//驱动器网关IP地址 
	unsigned char MacIPDNS[4],//DNS地址地址
	int MacUDPPort[1]//UDP端口号
	);


//硬件IO触发功能开关 126
DLL_XMT_SER_API void XMT_COMMAND_SetIO_OpenOrClose(
	unsigned char address,
	int Command_B3,//指令码
	int Command_B4,//指令码 
	unsigned char FlagOPenOrClose//[5]触发开关     ='O'开启硬件IO触发 ='C'关闭硬件IO触发 
	);


//设置外部IO功能 多次触发或者单次触发   127   
//[5]触发模式
//= 0x01 单次触发
//= 0x02 多次
DLL_XMT_SER_API void XMT_COMMAND_SetIO_MultOrSingle(
	unsigned char Address,
	int Command_B3,//指令码
	int Command_B4,//指令码 
	unsigned char FlagMultOrSingle//[5]触发模式 =0x01 单次触发 =0x02 多次触发 
	);


//硬件IO触发功能触发通道选择   128
//[5]通道号  =0x00 代表第一通道=0x01 代表第二通道 =0x02 代表第三通道='N' 代表没有选定 ='A' 代表选择所有通道 
DLL_XMT_SER_API void XMT_COMMAND_SetIO_ChChose(
	unsigned char Address,
	int Command_B3,//指令码
	int Command_B4,//指令码 
	unsigned char tmpCf//  =0x00 代表第一通道=0x01 代表第二通道 =0x02 代表第三通道='N' 代表没有选定 ='A' 代表选择所有通道
	);


// 129  硬件IO触发两点间隔时间设置
DLL_XMT_SER_API void XMT_COMMAND_SetIO_ConfigDisTimer(
	unsigned char address,
	int Command_B3,//指令码
	int Command_B4,//指令码 
	int DisTimer //间隔时间 每个点之间时间间隔，单位为us，最小间隔为100us
	);
	//[5][6][7][8]由一个int型数据的4个字节组成。如确定时间间隔为100us, 则这个int型数据为100，转换为十六进制为0x00000064, 分成四个字节来传递，则为0x00, 0x00, 0x00, 0x64


//发送单通道待触发数据  130 
DLL_XMT_SER_API void  XMT_COMMAND_saveDataToCh_Run(
	unsigned char address,
	int Command_B3,//指令码
	int Command_B4,//指令码 
	unsigned char FlagCh,//0x00 0x01 0x02
	unsigned char FlagVoltOrMove,//通道或者位移 0低压 1位移
	unsigned char NumBao,//当前数据包为第几个数据包
	unsigned char SendDatalong,//本包数据点数
	float SendArrUse[50],
	unsigned char FlagSendEndOrNot//发送是否完成
	);

//清除写入数据 131 
DLL_XMT_SER_API void XMT_COMMAND_ClearIO_Config(
	unsigned char address,
	int Command_B3,//指令码
	int Command_B4,//指令码 
	unsigned char FlagCh//0x00 0x01 0x02  'A'代表选定所有通道 
	);

// 硬件IO功能触发 发送停止/暂停  132
DLL_XMT_SER_API void XMT_COMMAND_StopIO_Run(
	unsigned char Address,
	int Command_B3,//指令码
	int Command_B4,//指令码 
	unsigned char FlagCh,//0x00 0x01 0x02  'A'代表选定所有通道 
	unsigned char FlagStop//停止方式 'T'代表停止  'P'代表暂停
	);

//133 设定指定通道当前位置为位移零点（闭环状态下）开环状态不好用
DLL_XMT_SER_API void XMT_COMMAND_ZeroSet(
	unsigned char address,
	int Command_B3,//指令码
	int Command_B4,//指令码 
	unsigned char FlagCh//0x00 0x01 0x02
	);

//134 设定当前通道电源极性  
//[6]‘S’单极性
//‘D’双极性
DLL_XMT_SER_API void XMT_COMMAND_SetIO_ChReInit(
	unsigned char address,
	int Command_B3,//指令码
	int Command_B4,//指令码 
	unsigned char tmpCf,//  =0x00 代表第一通道=0x01 代表第二通道 =0x02 代表第三通道='N' 代表没有选定 ='A' 代表选择所有通道
	unsigned char Flag
	);

// 135 仅E18使用
//1.A点位移、B点位移、拍照间隔位移、A点到B点运动时间为float型数据，解析方式和公司内部关于电压和位移的处理方式一样.
//2.拍照频率和拍照次数为uint32型，无符号整型。
DLL_XMT_SER_API void XMT_COMMAND_Set_PoitATimeAndTimes(
	unsigned char address,
	int Command_B3,//指令码
	int Command_B4,//指令码 
	unsigned char FlagCh,//0x00 0x01 0x02  
	double PointA,//A点位移 
	double DisMove,//拍照间隔位移
	int PZHz,//拍照频率(次 / S)
	int PZCishu,//拍照次数
	unsigned char *Rech,//返回通道
	double *pointB, //返回B点值
	int *runtime//A到B点运行时间
	);

//136
//开始、停止拍照动作 仅E18使用
DLL_XMT_SER_API void XMT_COMMAND_Begin_PoitATimeAndTimes(
	unsigned char address,
	int Command_B3,//指令码
	int Command_B4,//指令码 
	unsigned char FlagCh,//0x00 0x01 0x02  'A'代表选定所有通道 
	unsigned char BeginFlag //'R'开启'H'停止
	);

//137 设置触发IO口电平 仅E18使用
DLL_XMT_SER_API void XMT_COMMAND_SetFlagOut_PoitATimeAndTimes(
	unsigned char address,
	int Command_B3,//指令码
	int Command_B4,//指令码 
	unsigned char FlagCh,//0x00 0x01 0x02  
	unsigned char SetFlagOut //‘H’高电平‘L’低电平
	);


//138 单路相对位移
DLL_XMT_SER_API void XMT_COMMAND_XD_SinglePoint(
	unsigned char address,
	int Command_B3,//指令码
	int Command_B4,//指令码 
	unsigned char Channel_Num,
	float VoltOrMove_Data
	);

//139
DLL_XMT_SER_API void XMT_COMMAND_SaveDataArrToMCU_pro(
		unsigned char address,
		unsigned char Command_B3,
		unsigned char Command_B4,
		unsigned char  Channle_Useflag, //参与本此数据传输的通道号及是否需要存E2PROM    
		int Ch1_Num,//通道点数
		int Ch2_Num,//通道点数
		int Ch3_Num,//通道点数
		int Ch4_Num,//通道点数
		float ArrData_Ch1[],
		float ArrData_Ch2[],
		float ArrData_Ch3[],
		float ArrData_Ch4[],
		float MaxData1,//最大数据值 如果开环 一般为150或120
		float MinData1,//最小数据值 根据实际需要来做决定
		float MaxData2,//最大数据值 如果开环 一般为150或120
		float MinData2,//最小数据值 根据实际需要来做决定
		float MaxData3,//最大数据值 如果开环 一般为150或120
		float MinData3,//最小数据值 根据实际需要来做决定
		float MaxData4,//最大数据值 如果开环 一般为150或120
		float MinData4//最小数据值 根据实际需要来做决定
		);

//140
DLL_XMT_SER_API void XMT_COMMAND_SetMCUSendDataTimer_Pro(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char  Channle_Useflag, //高4为代表该通道是否参与输出 
	float ChUseTime_1,
	float ChUseTime_2,
	float ChUseTime_3,
	float ChUseTime_4
	);

//141
DLL_XMT_SER_API void XMT_COMMAND_SetMCU_BeginStopOrPauseSend(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char  Channle_Useflag, //高4为代表该通道是否参与输出 
	unsigned char ChUseFlag_1,
	unsigned char ChUseFlag_2,
	unsigned char ChUseFlag_3,
	unsigned char ChUseFlag_4
	);//20230423

//149 串口IAP 指令
DLL_XMT_SER_API void XMT_COMMAND_IAP(unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4);


////////////////////////////////////////////////////////////////////





DLL_XMT_SER_API void ArrDataSendAToB(unsigned char address, unsigned char Channel_Num, unsigned char flagOpenOrClose, double Point_A, double Point_B, int ArrLong, int tmpUs_100us);


//unsigned char address;地址数据
//unsigned char Channel_Num;对应通道
//unsigned char flagOpenOrClose;开闭环数据 O 开环 C 闭环
//double Point_A;//起始点数据
//double Point_B;//终止点数据
//float AToBRunTime;A点到B点的运行时间 
//int BTL;//波特率 9600 115200 当前的波特率
DLL_XMT_SER_API void PointAToPointBAndRunTime(unsigned char address, unsigned char Channel_Num, unsigned char flagOpenOrClose, double Point_A, double Point_B, float AToBRunTime, int BTL);//

unsigned char* DataAnla_ProYD(double f, unsigned char kk[4]);
unsigned char* DataAnla_ProXZ(double f, unsigned char kk[4]);
DLL_XMT_SER_API void  DataAnla_ProXZ_20210910(double f, unsigned char kk[4]);

long CalDataYD(unsigned char kk0, unsigned char kk1, unsigned char kk2, unsigned char kk3);// 20200529 按照夏哥的算法计算 
float CalDataXZ_20210910(unsigned char kk0, unsigned char kk1, unsigned char kk2, unsigned char kk3);// 20210910 按照夏哥的算法计算

																									 //unsigned char receive_usb_info_CheckTimeOut(int NumOfLibusbDevice, unsigned char receive_arr[], int TimeOutUse); //读取的USB的数据值 //TimeOutUse 20200624 默认的查询时间
unsigned char receive_usb_info_CheckTimeOut(unsigned char receive_arr[], int Arrlong, int TimeOutUse); //读取的USB的数据值 //TimeOutUse 20200624 默认的查询时间







																									   //2022定制类 同时设置六通道电压
																									   ///////////////////////////////////////////////////////////////////
unsigned int FloatToDa_16bit(float SendData, float MaxDataSend, float MinDataSend);//发送数据 最大值 最小值 
void ChangIntToTwoUChar(int tmpInt, unsigned char TmpChar[2]);//将整形数据转化为两个Char类型的数据
DLL_XMT_SER_API void SendSixVolt(unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	double TmpVolt1, double TmpVolt2, double TmpVolt3, double TmpVolt4, double TmpVolt5, double TmpVolt6,
	float MaxDataSend, float MinDataSend);
//通道控制电压
DLL_XMT_SER_API void SendSixChVolt(unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char ChUse,
	double TmpVolt1,
	float MaxDataSend, float MinDataSend);

//读取3通道电压 同时读取
DLL_XMT_SER_API long ReadCh3Volt(unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	double TmpVoltArr[3],
	float MaxDataSend, float MinDataSend,
	int disTimeus);

//定制项目
//设置波特率 地址 01 开始; 通道 01 开始 
//DB1设置值		    A	     B	    C	    D	    E
//对应波特率		9600	115200	256000	1382400	5250000
DLL_XMT_SER_API void SetBound(unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char FlagBound
	);
//读取单通道电压数
DLL_XMT_SER_API double ReadChOneVolt(unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char ChUse,
	float MaxDataSend, float MinDataSend,
	int disTimeus);


///////////////////////////////////////////////////////////////////


//B3 = 0 设置速度命令 B4 = 1;
DLL_XMT_SER_API void XMT_COMMAND_YDMoveSPD(
	unsigned char Address,
	int Command_B3,//指令码
	int Command_B4,//指令码
	unsigned char Ch, double SPD_f);//设置运行速度//地址 通道 

									////////////////B4 = 1;
									//B3 = 1 绝对位移命令
DLL_XMT_SER_API float XMT_COMMAND_YDAbMoveX(
	unsigned char Address,
	int Command_B3,//指令码
	int Command_B4,//指令码
	unsigned char Ch, double MoveX_f, unsigned char ReFlag);//绝对位置  unsigned char ReFlag 0 不反馈 1 表示反馈

															// 相对位移命令
															//B3 = 2 B4 = 1
DLL_XMT_SER_API float XMT_COMMAND_YDReMoveX(
	unsigned char Address,
	int Command_B3,//指令码
	int Command_B4,//指令码
	unsigned char Ch, float MoveX_f, unsigned char ReFlag);//相对对位置 unsigned char ReFlag 0 不反馈 1 表示反馈


														   //B3 = 3; B4= 1;
DLL_XMT_SER_API float XMT_COMMAND_YDCTUMove(
	unsigned char Address,
	int Command_B3,//指令码
	int Command_B4,//指令码
	unsigned char Ch,//通道号
	double SPD_f,//连续运行的速度命令
	unsigned char ROrLFlag,//正向反向标志位:'R'表示正向；'L'表示负向；
	unsigned char ReFlag);//连续运行 float SPD_f 运行速度 ,unsigned char ROrLFlag L左运行 R右运行 unsigned char ReFlag 0 不反馈 1 表示反馈
						  //
						  //B3 = 4; B4= 1;
DLL_XMT_SER_API float XMT_COMMAND_YDCTUMoveStop(
	unsigned char Address,
	int Command_B3,//指令码
	int Command_B4,//指令码
	unsigned char Ch, unsigned char ReFlag, unsigned char JOrXFlag);
//连续运行停止命令 unsigned char ReFlag 0 不反馈 1 表示反馈,unsigned char JOrXFlag 1绝对 0相对

// 读当前位置命令 
//B3 = 5; B4= 1;
DLL_XMT_SER_API double XMT_COMMAND_YDReMoveF(
	unsigned char Address,
	int Command_B3,//指令码
	int Command_B4,//指令码
	unsigned char Ch, unsigned char JOrXFlag); //unsigned char JOrXFlag 1绝对 0相对

	//108 设置相对零
	 //B3 = 6; B4= 1;

DLL_XMT_SER_API double XMT_COMMAND_YDSetCTZero(
	unsigned char Address,
	int Command_B3,//指令码
	int Command_B4,//指令码w
	unsigned char Ch, double ZerX_f, unsigned char ReFlag); //设置相对零 unsigned char ReFlag 0 不反馈 1 表示反馈

															//读相对零位
															//B3 = 7; B4= 1;
DLL_XMT_SER_API float XMT_COMMAND_YDReadCTZero(
	unsigned char Address,
	int Command_B3,//指令码
	int Command_B4,//指令码
	unsigned char Ch); //读取相对零

					   //110 返回到零位置 分绝对，零相对零
					   //B3 = 8; B4= 1;
DLL_XMT_SER_API float XMT_COMMAND_YDRBackZero(
	unsigned char Address,
	int Command_B3,//指令码
	int Command_B4,//指令码
	unsigned char Ch, unsigned char JOrXFlag, unsigned char ReFlag); //返回零位置 unsigned char JOrXFlag 1绝对 0相对 unsigned char ReFlag 0 不反馈 1 表示反馈

																	 //111 压电波形命令
																	 //B3 = 9; B4= 1;
DLL_XMT_SER_API void 	XMT_COMMAND_YDWave(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Channel_Num,
	unsigned char WaveType,
	double FengFengZhi,
	double PinLvHz,
	double Pianzhi
	);

//112 压电紧急停止命令 返回当前绝对位置
//B3 = 10; B4= 1;
DLL_XMT_SER_API double XMT_COMMAND_YDStopAll(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Channel_Num
	);

//返回压A到B点电马达速度
//B3 = 11; B4= 1;
DLL_XMT_SER_API void XMT_COMMAND_YDAbMoveA_BSpeed(
	unsigned char Address,
	int Command_B3,//指令码
	int Command_B4,//指令码
	unsigned char Ch, double MoveX_A, double MoveX_B);
//从usb接口中读取 A B点的返回速度
DLL_XMT_SER_API double XMT_COMMAND_RecA_BSpeed(unsigned char Ch, int TimeOutUse);

//B3 = 12 B4 = 1 标低限限位置
DLL_XMT_SER_API double XMT_COMMAND_YDAbSetLimit(
	unsigned char Address,
	int Command_B3,//指令码
	int Command_B4,//指令码
	unsigned char Ch,
	double SetDataLimit);

//读限限位置
//B3 = 13 B4 = 1
DLL_XMT_SER_API float XMT_COMMAND_YDReadAbLimit(
	unsigned char Address,
	int Command_B3,//指令码
	int Command_B4,//指令码
	unsigned char Ch); //读取相对零

//B3 = 14 B4 = 1 标高限限位置
DLL_XMT_SER_API double XMT_COMMAND_YDAbSetHighLimit(
	unsigned char Address,
	int Command_B3,//指令码
	int Command_B4,//指令码
	unsigned char Ch,
	double SetDataLimit);

//读限限位置
//B3 = 15 B4 = 1
DLL_XMT_SER_API float XMT_COMMAND_YDReadAbHighLimit(
	unsigned char Address,
	int Command_B3,//指令码
	int Command_B4,//指令码
	unsigned char Ch); //读取相对零

//绝对零位校准
//B3 = 16 B4 = 1
DLL_XMT_SER_API void XMT_COMMAND_CorrectAbZero(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Channel_Num,
	unsigned char ReFlag
	);


//辅助协助类
//设置 开闭环 17
//B3 = 17 B4 = 1
DLL_XMT_SER_API void 	XMT_COMMAND_YDZX_Assist_SetFlag(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Channel_Num,
	unsigned char SetFlag
	);
//18 
//读取开闭环
//B3 = 18 B4 = 1
DLL_XMT_SER_API unsigned char XMT_COMMAND_YDZX_Assist_ReadFlag(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Channel_Num
	);


//B3 = 19 B4 = 1
//波形调试功能
DLL_XMT_SER_API void XMT_COMMAND_YDZX_SendWave(
	unsigned char Address,
	int Command_B3,//指令码
	int Command_B4,//指令码
	unsigned char Ch,
	unsigned char WaveType,
	double FengFengZhi,
	double PinLvHz,
	double Pianzhi,
	unsigned char RunZhengOrFanFlag,//正方向 'P' 'N'
	unsigned char FlagRunForever,//一直运动0x01 ，定周期运动 0x00
	long long RunCi,
	unsigned char  Percent
	);

//B3 = 20 B4 = 1
//波形调试功能
DLL_XMT_SER_API void XMT_COMMAND_YDZX_StopWave(
	unsigned char Address,
	int Command_B3,//指令码
	int Command_B4,//指令码
	unsigned char Ch
	);
//直线马达
//B3 = 21; B4= 1;  
//设置PID参数
DLL_XMT_SER_API void XMT_COMMAND_YD_SendArray_PID_Channel(
	int address,//地址码
	int Command_B3,//指令码
	int Command_B4,//指令码
	float PID_P,
	float PID_I,
	float PID_D
	);

//直线马达
//B3 = 22; B4= 1;  
//读取PID参数
DLL_XMT_SER_API void XMT_COMMAND_YD_Read_PID_Channel(
	int address,//地址码
	int Command_B3,//指令码
	int Command_B4,//指令码
	float PID_Rc[3]
	);

// 实时返回当前位置
//B3 = 23 B4 = 1;
DLL_XMT_SER_API void 	XMT_COMMAND_YD_ReadData_TS(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Channel_Num,
	unsigned char TimerSet_ms
	);


//停止读取  实时返回当前位置
//B3 = 24 B4 = 1;
DLL_XMT_SER_API void 	XMT_COMMAND_YD_ReadData_Stop(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4
	);

//设定闭环精度
//B3 = 25 B4 = 1;
DLL_XMT_SER_API void XMT_COMMAND_YD_JDu(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char FlagJD
	);

/////////////////////////////////////////////////


void DataLongToArr_8(long long RunCi, unsigned char TmpDataArr[]);
unsigned char* DataAnla_ProLD(long IntTmp, unsigned char kk[4]);
int CalDataLD(unsigned char kk0, unsigned char kk1, unsigned char kk2, unsigned char kk3);
int SendArray_ReadDataChannel_LDRcy(int bao_long, unsigned char ArrRec[], unsigned char ChType);
// 压电螺钉
//B3 = 0; B4= 2;
DLL_XMT_SER_API void XMT_COMMAND_YD_LDing_SendWave(
	unsigned char Address,
	int Command_B3,//指令码
	int Command_B4,//指令码
	unsigned char Ch,
	unsigned char WaveType,
	double FengFengZhi,
	double PinLvHz,
	double Pianzhi,
	unsigned char RunZhengOrFanFlag,//正方向 'P' 'N'
	unsigned char FlagRunForever,//一直运动0x01 ，定周期运动 0x00
	long long RunCi,
	unsigned char  Percent
	);
// 压电螺钉
//B3 = 1; B4= 2;
DLL_XMT_SER_API void XMT_COMMAND_YD_LDing_SendWaveStop(
	unsigned char Address,
	int Command_B3,//指令码
	int Command_B4,//指令码
	unsigned char Ch
	);

// 压电螺钉 压电螺钉相对运动
//B3 = 2; B4= 2;
// 压电螺钉 压电螺钉绝对运动
//B3 = 3; B4= 2;
DLL_XMT_SER_API void XMT_COMMAND_YD_LDing_Move(
	unsigned char Address,
	int Command_B3,//指令码
	int Command_B4,//指令码
	unsigned char Ch,
	unsigned char RunZhengOrFanFlag,//[6]'P'正向'N'反向
	long RunCi
	);

//B3 = 4; B4= 2;
DLL_XMT_SER_API int XMT_COMMAND_YD_LDing_ReadZQ(
	unsigned char Address,
	int Command_B3,//指令码
	int Command_B4,//指令码
	unsigned char Ch
	);

//B3 = 5; B4= 2;
//压电螺钉 通道计数清零
DLL_XMT_SER_API void XMT_COMMAND_YD_LDing_ZeroCyc(
	unsigned char Address,
	unsigned char Command_B3,//指令码
	unsigned char Command_B4,//指令码
	unsigned char Ch);

//B3 = 7; B4= 2;
//压电螺钉 设置高分辨率以及低分辨率
DLL_XMT_SER_API void XMT_COMMAND_YD_LDing_HOrLowFBL_S(unsigned char Address,
	unsigned char Command_B3,//指令码
	unsigned char Command_B4,//指令码
	unsigned char HorLowFlag// [5]'1'高分辨率 [5]'0'低分辨率
	);

//压电螺钉闭环
//B3 = 8 设置速度命令 B4 = 2;  速度 0-100表示速度0-100%
DLL_XMT_SER_API void XMT_COMMAND_YD_LDingMoveSPD(
	unsigned char Address,
	int Command_B3,//指令码
	int Command_B4,//指令码
	unsigned char Ch, double SPD_f);//设置运行速度//地址 通道 

									//压电螺钉
									//B3 = 9; B4= 2; 绝对位移命令
DLL_XMT_SER_API float XMT_COMMAND_YD_LDingAbMoveX(
	unsigned char Address,
	int Command_B3,//指令码
	int Command_B4,//指令码
	unsigned char Ch, double MoveX_f, unsigned char ReFlag);//绝对位置  unsigned char ReFlag 0 不反馈 1 表示反馈

															//压电螺钉
															//B3 = 10; B4= 2;增量位移命令
DLL_XMT_SER_API float XMT_COMMAND_YD_LDingAddMoveX(
	unsigned char Address,
	int Command_B3,//指令码
	int Command_B4,//指令码
	unsigned char Ch, double MoveX_f, unsigned char ReFlag);//增量位移命令  unsigned char ReFlag 0 不反馈 1 表示反馈

															//压电螺钉
															//B3 = 11; B4= 2; 读当前位置命令;
DLL_XMT_SER_API double XMT_COMMAND_YD_LDingReMoveF(
	unsigned char Address,
	int Command_B3,//指令码
	int Command_B4,//指令码
	unsigned char Ch, unsigned char JOrXFlag); //unsigned char JOrXFlag 1绝对 0相对

											   //压电螺钉
											   //B3 = 12; B4= 2;  //返回到零位置
DLL_XMT_SER_API float XMT_COMMAND_YD_LDingRBackZero(
	unsigned char Address,
	int Command_B3,//指令码
	int Command_B4,//指令码
	unsigned char Ch, unsigned char JOrXFlag, unsigned char ReFlag); //返回零位置 unsigned char JOrXFlag 1绝对 0相对 unsigned char ReFlag 0 不反馈 1 表示反馈
																	 //压电螺钉
																	 //B3 = 13; B4= 2;  //运行停止命令
DLL_XMT_SER_API double XMT_COMMAND_YD_LDingStopAll(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Channel_Num
	);
//压电螺钉
//B3 = 14; B4= 2;  //零位校准(找光栅尺0位置)
DLL_XMT_SER_API double XMT_COMMAND_YD_LDingCorrectAbZero(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Channel_Num,
	unsigned char ReFlag
	);
//压电螺钉
//B3 = 15; B4= 2;  设置开闭环状态 
DLL_XMT_SER_API void 	XMT_COMMAND_Assist_YD_LDingSetFlag(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Channel_Num,
	unsigned char SetFlag
	);
//压电螺钉
//B3 = 16; B4= 2;  读取开闭环状态 
DLL_XMT_SER_API unsigned char XMT_COMMAND_Assist_YD_LDingReadFlag(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Channel_Num
	);
//压电螺钉
//B3 = 17; B4= 2;  设定下位机单位  [5] 0角秒 1毫弧度  2微弧度 3度 4光栅尺一刻度 5开环控制周期 6微米 7毫米 8纳米
DLL_XMT_SER_API void XMT_COMMAND_YD_LDing_SetUnit(
	unsigned char Address,
	int Command_B3,//指令码
	int Command_B4,//指令码
	unsigned char UnitFlag
	);//[5] 0角秒 1毫弧度  2微弧度 3度 4光栅尺一刻度	

	  //压电螺钉
	  //B3 = 18; B4= 2;  读取下位机单位 [5] 0角秒 1毫弧度  2微弧度 3度 4光栅尺一刻度 5开环控制周期 6微米 7毫米 8纳米
DLL_XMT_SER_API unsigned char XMT_COMMAND_YD_LDing_ReadUnit(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4
	);
//压电螺钉
//B3 = 19; B4= 2;  //设置PID参数
DLL_XMT_SER_API void XMT_COMMAND_YD_LDing_SendArray_PID_Channel(
	int address,//地址码
	int Command_B3,//指令码
	int Command_B4,//指令码
	float PID_P,
	float PID_I,
	float PID_D
	);//发送数据

	  //压电螺钉
	  //B3 = 20; B4= 2;  
	  //读取PID参数
DLL_XMT_SER_API void XMT_COMMAND_YD_LDing_Read_PID_Channel(
	int address,//地址码
	int Command_B3,//指令码
	int Command_B4,//指令码
	float PID_Rc[3]
	); //发送数据
	   //压电螺钉
	   //B3 = 21; B4= 2;  // 实时返回当前位置
DLL_XMT_SER_API void 	XMT_COMMAND_YD_LDing_ReadData_TS(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Channel_Num,
	unsigned char TimerSet_ms
	);
//压电螺钉
//B3 = 22; B4= 2;  //停止读取  实时返回当前位置
DLL_XMT_SER_API void 	XMT_COMMAND_YD_LDing_ReadData_Stop(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4
	);

//设定当前位置为刻度盘0刻度
//压电螺钉
//B3 = 23; B4= 2;  [5]'Z'标定参数0 ‘O’标定当前位置 大写字母'O'
DLL_XMT_SER_API void 	XMT_COMMAND_YD_LDing_SerZeroOrDel(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char FlagZeroOrDelete
	);
//压电螺钉
//B3 = 24; B4= 2; 标低限位置
//B3 = 26; B4= 2; 标高限位置
DLL_XMT_SER_API void 	XMT_COMMAND_YD_LDing_SetSystemInfo(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Channel_Num,
	double SystemInfo
	);
///压电螺钉
//B3 = 25; B4= 2; 21 读取低限
//B3 = 27; B4= 2; 21 读高低限
//读数据类 
DLL_XMT_SER_API double XMT_COMMAND_YD_LDing_ReadSystemInfo(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Channel_Num
	);

///压电螺钉
//B3 = 28; B4= 2;  设定闭环精度
DLL_XMT_SER_API void XMT_COMMAND_YD_LDing_SetPrecision(unsigned char Address,
	unsigned char Command_B3,//指令码
	unsigned char Command_B4,//指令码
	unsigned char PrecisionValue// [5] 0到255
	);

///压电螺钉
//B3 = 29; B4= 2; 标定实际最大速度
DLL_XMT_SER_API void XMT_COMMAND_YD_LDing_SetMaxSpeed(unsigned char Address,
	unsigned char Command_B3,//指令码
	unsigned char Command_B4//指令码
	);

///压电螺钉
//B3 = 30; B4= 2;
//读取标定实际最大速度
DLL_XMT_SER_API double XMT_COMMAND_YD_LDing_ReadMaxSpeed(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4
	);

///压电螺钉
//B3 = 31; B4= 2;
//设置实际速度
DLL_XMT_SER_API void XMT_COMMAND_YD_LDing_SetReadMaxSpeed(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Channel_Num,
	double SystemInfo
	);


///压电螺钉
//B3 = 32; B4= 2;
//设置光栅尺数据反向 [5]0反向 1不反向
DLL_XMT_SER_API void XMT_COMMAND_YD_LDing_SetGSCZorF(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char SetFlag 
	);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



//旋转马达
//B3 = 0 设置速度命令 B4 = 3;
DLL_XMT_SER_API void XMT_COMMAND_XZMoveSPD(
	unsigned char Address,
	int Command_B3,//指令码
	int Command_B4,//指令码
	unsigned char Ch, double SPD_f);//设置运行速度//地址 通道 

									//旋转马达
									//B3 = 1; B4= 3; 绝对位移命令
DLL_XMT_SER_API float XMT_COMMAND_XZAbMoveX(
	unsigned char Address,
	int Command_B3,//指令码
	int Command_B4,//指令码
	unsigned char Ch, double MoveX_f, unsigned char ReFlag);//绝对位置  unsigned char ReFlag 0 不反馈 1 表示反馈

															//旋转马达
															//B3 = 2; B4= 3; 增量位移命令
DLL_XMT_SER_API float XMT_COMMAND_XZAddMoveX(
	unsigned char Address,
	int Command_B3,//指令码
	int Command_B4,//指令码
	unsigned char Ch, double MoveX_f, unsigned char ReFlag);//绝对位置  unsigned char ReFlag 0 不反馈 1 表示反馈

															//旋转马达
															//B3 = 3; B4= 3 读当前位置命令;
DLL_XMT_SER_API double XMT_COMMAND_XZReMoveF(
	unsigned char Address,
	int Command_B3,//指令码
	int Command_B4,//指令码
	unsigned char Ch, unsigned char JOrXFlag); //unsigned char JOrXFlag 1绝对 0相对

											   //旋转马达
											   //B3 = 4; B4= 3;  //返回到零位置
DLL_XMT_SER_API float XMT_COMMAND_XZRBackZero(
	unsigned char Address,
	int Command_B3,//指令码
	int Command_B4,//指令码
	unsigned char Ch, unsigned char JOrXFlag, unsigned char ReFlag); //返回零位置 unsigned char JOrXFlag 1绝对 0相对 unsigned char ReFlag 0 不反馈 1 表示反馈


																	 //旋转马达
																	 //B3 = 5; B4= 3;  //运行停止命令
																	 //旋转马达
																	 //B3 = 5; B4= 3;  //运行停止命令
DLL_XMT_SER_API double XMT_COMMAND_XZStopAll(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Channel_Num,
	unsigned char FlagReData
	);

//旋转马达
//B3 = 6; B4= 3;  //零位校准(找光栅尺0位置)
DLL_XMT_SER_API double XMT_COMMAND_XZCorrectAbZero(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Channel_Num,
	unsigned char SetFlag
	);

//旋转马达
//B3 = 7; B4= 3;  设置开闭环状态 
DLL_XMT_SER_API void 	XMT_COMMAND_Assist_XZSetFlag(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Channel_Num,
	unsigned char SetFlag
	);
//旋转马达
//B3 = 8; B4= 3;  读取开闭环状态 
DLL_XMT_SER_API unsigned char XMT_COMMAND_Assist_XZReadFlag(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Channel_Num
	);
//旋转马达
//B3 = 9; B4= 3;  发送波形驱动 
DLL_XMT_SER_API void XMT_COMMAND_XZ_SendWave(
	unsigned char Address,
	int Command_B3,//指令码
	int Command_B4,//指令码
	unsigned char Ch,
	unsigned char WaveType,
	double FengFengZhi,
	double PinLvHz,
	double Pianzhi,
	unsigned char RunZhengOrFanFlag,//正方向 'P' 'N'
	unsigned char FlagRunForever,//一直运动0x01 ，定周期运动 0x00
	long long RunCi,
	unsigned char  Percent
	);

//旋转马达
//B3 = 10; B4= 3; 停驱动波形
DLL_XMT_SER_API void XMT_COMMAND_XZ_SendWaveStop(
	unsigned char Address,
	int Command_B3,//指令码
	int Command_B4,//指令码
	unsigned char Ch
	);

//旋转马达
//B3 = 11; B4= 3; 设定下位机单位
DLL_XMT_SER_API void XMT_COMMAND_XZ_SetUnit(
	unsigned char Address,
	int Command_B3,//指令码
	int Command_B4,//指令码
	unsigned char UnitFlag
	);//[5] 0角秒 1毫弧度  2微弧度 3度 4光栅尺一刻度 5开环控制周期 6微米 7毫米 8纳米

	  //旋转马达
	  //B3 = 12; B4= 3;  读取下位机单位 旋转马达 
DLL_XMT_SER_API unsigned char XMT_COMMAND_XZ_ReadUnit(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4
	);
//旋转马达
//B3 = 13; B4= 3;  读取下位机单位 旋转马达 
//设置PID参数
DLL_XMT_SER_API void XMT_COMMAND_XZ_SendArray_PID_Channel(
	int address,//地址码
	int Command_B3,//指令码
	int Command_B4,//指令码
	float PID_P,
	float PID_I,
	float PID_D
	);//发送数据
	  //旋转马达
	  //B3 = 14; B4= 3;  读取下位机单位 旋转马达 
	  //读取PID参数
DLL_XMT_SER_API void XMT_COMMAND_XZ_Read_PID_Channel(
	int address,//地址码
	int Command_B3,//指令码
	int Command_B4,//指令码
	float PID_Rc[3]
	); //发送数据
	   // 实时返回当前位置
	   //B3 = 15 B4 = 3;
DLL_XMT_SER_API void 	XMT_COMMAND_XZ_ReadData_TS(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Channel_Num,
	unsigned char TimerSet_ms
	);
//旋转马达
//停止读取  实时返回当前位置
//B3 = 16 B4 = 3; 
DLL_XMT_SER_API void 	XMT_COMMAND_XZ_ReadData_Stop(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4
	);
//旋转马达
//设定当前位置为刻度盘0刻度
//B3 = 17 B4 = 3;  [5]'Z'标定参数0 ‘O’标定当前位置
DLL_XMT_SER_API void 	XMT_COMMAND_XZ_SerZeroOrDel(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char FlagZeroOrDelete
	);


//旋转马达 
//B3 = 18标低限位置 20 标定高限位置 B4 = 20;
DLL_XMT_SER_API void 	XMT_COMMAND_XZ_SetSystemInfo(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Channel_Num,
	double SystemInfo
	);
//旋转马达 
//B3 = 19 读取低限  21 读取高限
//读数据类 
DLL_XMT_SER_API double XMT_COMMAND_XZ_ReadSystemInfo(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Channel_Num
	);

//旋转马达
//B3 = 22; B4= 3;
//设置高分辨率以及低分辨率
DLL_XMT_SER_API void XMT_COMMAND_XZ_HOrLowFBL_S(unsigned char Address,
	unsigned char Command_B3,//指令码
	unsigned char Command_B4,//指令码
	unsigned char HorLowFlag// [5]'1'高分辨率 [5]'0'低分辨率
	);

//旋转马达
//B3 = 23; B4= 3;
//设定闭环精度
DLL_XMT_SER_API void XMT_COMMAND_XZ_SetPrecision(unsigned char Address,
	unsigned char Command_B3,//指令码
	unsigned char Command_B4,//指令码
	unsigned char PrecisionValue// [5] 0到255
	);

//旋转马达 下位机 走一圈 按照度来运行，自动标最大速度；
//B3 = 24; B4= 3;
//标定实际最大速度
DLL_XMT_SER_API void XMT_COMMAND_XZ_SetMaxSpeed(unsigned char Address,
	unsigned char Command_B3,//指令码
	unsigned char Command_B4//指令码
	);

//旋转马达 
//B3 = 25 B4 = 3
//读取标定实际最大速度
DLL_XMT_SER_API double XMT_COMMAND_XZ_ReadMaxSpeed(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4
	);

//旋转马达 
//B3 = 26 B4 = 3;
//设置实际速度
DLL_XMT_SER_API void XMT_COMMAND_XZ_SetReadMaxSpeed(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4,
	unsigned char Channel_Num,
	double SystemInfo
	);

//旋转马达
//B3 =27; B4= 3; 计数周期清零
DLL_XMT_SER_API void XMT_COMMAND_XZ_ClearZero(
	unsigned char address,
	unsigned char Command_B3,
	unsigned char Command_B4
	);

/////////////////////////////////////////////////////////////////////////////////

//double tmpX,    // 单位微米 动平台的位恣
//double tmpY,    // 单位微米 动平台的位恣
//double tmpZ,    // 单位微米 动平台的位恣
//double tmpROLL, //相对静平台的  θX 毫弧度
//double tmpPITCH, //相对静平台的 θY 毫弧度
//double tmpYAW,   //相对静平台的   θZ 毫弧度
//double tmp_XZhuan,//顺时针旋转 单位度 度
//unsigned char flagOpenFalg1, 'O' 开环 'C' 闭环
//unsigned char flagOpenFalg2, 'O' 开环 'C' 闭环
//int address //使用的地址
//六自由度控制算法 20211126 
DLL_XMT_SER_API void PNSiXControl(double tmpX,    // 单位微米 动平台的位恣
	double tmpY,    // 单位微米 动平台的位恣
	double tmpZ,    // 单位微米 动平台的位恣
	double tmpROLL, //相对静平台的  θX 毫弧度
	double tmpPITCH, //相对静平台的 θY 毫弧度
	double tmpYAW,   //相对静平台的   θZ 毫弧度
	double tmp_XZhuan,//顺时针旋转 单位度 度
	unsigned char flagOpenFalg1,
	unsigned char flagOpenFalg2,
	int address //使用的地址
	);

////同时发送6个通道的数据
////20220119 通道
//double B1;//通道1 
//double B2;//通道6
//double B3;//通道5
//double B4;//通道4
//double B5;//通道3
//double B6;//通道2
//unsigned char flagOpenFalg1;  //'O' 开环 'C' 闭环
//unsigned char flagOpenFalg1;  //'O' 开环 'C' 闭环
DLL_XMT_SER_API void send6zhou_TaoCi(double B1, double B2, double B3, unsigned char flagOpenFalg1, double B4, double B5, double B6, unsigned char flagOpenFalg2, int address_maUse);


//double tmpX;   // 单位微米 动平台的位恣
//double tmpY;    // 单位微米 动平台的位恣
//double tmpZ;    // 单位微米 动平台的位恣
//double tmpROLL; //相对静平台的  θX 毫弧度
//double tmpPITCH; //相对静平台的 θY 毫弧度
//double tmpYAW;   //相对静平台的   θZ 毫弧度
//double tmp_XZhuan;//顺时针旋转 单位度 度
//double SixControl[6];//返回的6根陶瓷的长度 单位 微米
DLL_XMT_SER_API void PNSiXControlGetControl(double tmpX,    // 单位微米 动平台的位恣
	double tmpY,    // 单位微米 动平台的位恣
	double tmpZ,    // 单位微米 动平台的位恣
	double tmpROLL, //相对静平台的  θX 毫弧度
	double tmpPITCH, //相对静平台的 θY 毫弧度
	double tmpYAW,   //相对静平台的   θZ 毫弧度
	double tmp_XZhuan,//顺时针旋转 单位度 度
	double SixControl[6]
	);

//double XP,//动平台相对静平台的初始位置 圆心的位置 动平台相对于静平台
//double YP,
//double ZP,
//double X,//动平台的位恣
//double Y,
//double Z,
//double ROLL, //相对静平台的 θX
//double PITCH, //相对静平台的 θY
//double YAW, //相对静平台的 θZ
//double R, //动平台的半径
//double r,//静平台的半径
//double up_angle0, //动平台 铰点的初始位置
//double up_angle1,
//double up_angle2,
//
//double down_angle0,//静平台 铰点的初始位置
//double down_angle1,
//double down_angle2
//20221027 P32P25 摆镜控制
DLL_XMT_SER_API void P32Control(double XP,//动平台相对静平台的初始位置 圆心的位置 动平台相对于静平台
	double YP,
	double ZP,
	double X,//动平台的位恣
	double Y,
	double Z,
	double ROLL, //相对静平台的 θX
	double PITCH, //相对静平台的 θY
	double YAW, //相对静平台的 θZ
	double R, //动平台的半径
	double r,//静平台的半径
	double up_angle0, //动平台 铰点的初始位置
	double up_angle1,
	double up_angle2,

	double down_angle0,//静平台 铰点的初始位置
	double down_angle1,
	double down_angle2
	);

