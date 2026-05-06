#pragma once

#include <cstdint>
#include <vector>
#include "../../DiskBackup/include/HardDriveIO.h"
#include "../../DiskBackup/include/DeviceManagerModule.itf"

class IManager;
class DiskCheckBase
{
	public:
		DiskCheckBase() {}
		~DiskCheckBase() {}

		// 获取设备管理器
		int OpenDevMgrModule(CMomPtr<IDeviceManagerModule>& apDevModule, IManager** ppDevMgr);
		TBRESULT DoCheck();

	protected:
		CMomPtr<IDeviceManagerModule> m_apDevModule;	// 设备管理模块
		IManager* m_pDevMgr = nullptr;	// 设备管理器
		int64_t m_u64StartSector = 0;		// 磁盘起始扇区
		int64_t m_u64CurrentSector = 0; // 当前正在检查的扇区
		int64_t m_u64SectorCount = 0;		// 磁盘总扇区数
		CHardDriveIO m_drvIO;

		std::vector<int64_t> m_vectBadSecs;
		bool m_bQuick = true;
};


