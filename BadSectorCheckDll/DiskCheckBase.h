#pragma once

#include <cstdint>
#include <vector>
#include <atomic>
#include <mutex>
#include <string>
#include "../../DiskBackup/include/HardDriveIO.h"
#include "../../DiskBackup/include/DeviceManagerModule.itf"

// DLL 导出 / 导入宏
// DLL 工程定义 BADSECTORCHECKDLL_EXPORTS（vcxproj 已配置），使用方自动 dllimport
#ifdef BADSECTORCHECKDLL_EXPORTS
#  define BADSECTORCHECK_API __declspec(dllexport)
#else
#  define BADSECTORCHECK_API __declspec(dllimport)
#endif

class IManager;

class BADSECTORCHECK_API DiskCheckBase
{
public:
	// 磁盘枚举信息
	struct DiskInfo {
		int      index;           // PHYSICALDRIVE 编号
		std::string name;         // 系统名称，如 "\\.\PHYSICALDRIVE0"
		int64_t  startSector;     // 可用区起始扇区（GPT=34, MBR=0）
		int64_t  sectorCount;     // 扇区个数（与 IDeviceEntry::GetSectorCount 口径一致）
		uint32_t bytesPerSector;  // 每扇区字节数
	};

	DiskCheckBase() {}
	~DiskCheckBase() {}

	// 打开设备管理器模块
	int OpenDevMgrModule(CMomPtr<IDeviceManagerModule>& apDevModule, IManager** ppDevMgr);

	// 枚举所有物理磁盘
	static std::vector<DiskInfo> EnumerateDisks();

	// 执行坏扇区检测（在子线程中调用）
	TBRESULT DoCheck(U32 uDiskIndex);

	// 取消检测
	void Cancel() { m_bIsCancel = true; }

	// 是否正在运行
	bool IsRunning() const { return m_bIsRunning; }

	// 设置快速模式
	void SetQuick(bool quick) { m_bQuick = quick; }

	// 线程安全地获取当前进度
	// currentSector: 绝对扇区位置；badSecs: 相对于 startSector 的坏扇区偏移列表
	void GetProgress(int64_t& currentSector, std::vector<int64_t>& badSecs);

	int64_t  GetStartSector()  const { return m_u64StartSector; }
	int64_t  GetSectorCount()  const { return m_u64SectorCount; }
	uint32_t GetSectorSize()   const { return m_u32SectorSize; }

protected:
	CMomPtr<IDeviceManagerModule> m_apDevModule;
	IManager*  m_pDevMgr        = nullptr;
	int64_t    m_u64StartSector = 0;
	int64_t    m_u64CurrentSector = 0;
	int64_t    m_u64SectorCount = 0;
	uint32_t   m_u32SectorSize  = 0;
	CHardDriveIO m_drvIO;

	std::vector<int64_t>   m_vectBadSecs;
	std::mutex             m_mutex;
	std::atomic<bool>      m_bIsCancel { false };
	std::atomic<bool>      m_bIsRunning{ false };
	bool                   m_bQuick    = true;
};


