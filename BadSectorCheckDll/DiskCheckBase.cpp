#include "pch.h"
#include "DiskCheckBase.h"
#include "../../DiskBackup/include/DeviceManagerModule.itf"
#include "../../DiskBackup/include/DeviceManager.itf"
#include "../../DiskBackup/include/Container.itf"
#include "../../DiskBackup/include/NTFSLib.h"
#include "../../DiskBackup/mod.Partition/PartitionEntry.itf"
#include "../../DiskBackup/include/DiskEntry.itf"
#include "../../DiskBackup/include/PartitionManager.itf"

#define ONE_TIMES_TEST_SECTOR		256*2	

// 打开设备管理模块，获取设备管理器
int DiskCheckBase::OpenDevMgrModule(CMomPtr<IDeviceManagerModule>& apDevModule, IManager** ppDevMgr)
{
	int iResult = 0;
    apDevModule = OpenModule(DeviceManager);
    if (apDevModule == NULL)
        return BP_ERR_OPEN_DEVICE_MANAGER;

    IManager* pDevMgr = NULL;
    try
    {
        pDevMgr = apDevModule->CreateDeviceManager();
    }
    catch (const std::exception&)
    {
        // CreateDeviceManager 内部会抛出并自行捕获 std::logic_error 等调试断言异常，
        // 此处捕获是防止异常意外传播到外部
        iResult = BP_ERR_CREATE_DEVICE_MANAGER;
    }
    if (pDevMgr == NULL)
        return iResult;
    apDevModule->SetCurrentDeviceManager(pDevMgr);

    *ppDevMgr = pDevMgr;

    return iResult;
}

TBRESULT DiskCheckBase::DoCheck()
{
    TBRESULT iResult = BP_ERR_SUCCESS;
    long long test = 0;
	IIterator* pIter = NULL;
    do {
        if (m_pDevMgr != nullptr)
        {
            return iResult;
        }

        iResult = OpenDevMgrModule(m_apDevModule, &m_pDevMgr);

        if (iResult != 0)
        {
            break;
        }

        if (m_pDevMgr == NULL)
        {
            iResult = BP_ERR_CREATE_DEVICE_MANAGER;
			break;
        }

        pIter = m_pDevMgr->NewIterator();
		if (pIter == NULL)
		{
			iResult = -1;
			break;
		}
		CMomPtr<IManagerItem> pDiskItem;

		// 假设传入的设备是第一块磁盘
		pDiskItem = pIter->Next();
		if (pDiskItem == NULL)
		{
			iResult = -1;
			break;
		}

		int nHarddiskIndex = 0; // 磁盘index，0表示第一块磁盘
		S64 n64StartSector = 0; // 磁盘起始扇区，0表示从磁盘的第一个扇区开始

        CHardDriveIOSetting drvIoSetting(m_pDevMgr);
        CHardDriveIO::LoadSetting(drvIoSetting);

        IDeviceEntry* pDeviceEntry = NULL;
        IPartitionEntry* pPartitionEntry = pDiskItem->GetInterface(IPartitionEntry);
        if (pPartitionEntry)
        {
            pDeviceEntry = static_cast<IDeviceEntry*>(pPartitionEntry);
        }
        else
        {
            pDeviceEntry = pDiskItem->GetInterface(IDeviceEntry);
        }

        if (!pDeviceEntry)
        { 
            // 出错了，无法获取分区信息
            iResult = -1;
            break; 
        }


        m_u64StartSector = pDeviceEntry->GetStartSector();
        m_u64SectorCount = pDeviceEntry->GetSectorCount();


        if (!CNTFSLib::ConvertPartitionDevice2DiskDevice(m_pDevMgr, pDiskItem, nHarddiskIndex, n64StartSector))
        {
            // 取到磁盘index：nHarddiskIndex
            if (!CNTFSLib::GetDiskDeviceNumber(m_pDevMgr, pDiskItem, nHarddiskIndex))
            {
				// 出错了，无法获取磁盘index
                iResult = BP_ERR_INVALID_PARAMETER;
                break;
            }
        }

        // 打开磁盘，获取扇区大小
        if (!m_drvIO.OpenDisk(nHarddiskIndex))
        {
            iResult = -1;
            break;
        }

        // 每个扇区的字节数
        U32 u32SectorSize = m_drvIO.GetSectorSize();
        
        const U64 stepSize = 10 * 1024 * 1024;
        const U64 minStartSize = 2 * 1024 * 1024;


        U64 minSectors = minStartSize / u32SectorSize; //前面这么多个扇区不能跳过
        U64 stepSectors = stepSize / u32SectorSize;

        while (m_u64CurrentSector < m_u64SectorCount + m_u64StartSector)
        {
            S64 nHowManySectors = ONE_TIMES_TEST_SECTOR;
            if (nHowManySectors + m_u64CurrentSector > m_u64SectorCount + m_u64StartSector)
            {
                nHowManySectors = m_u64SectorCount + m_u64StartSector - m_u64CurrentSector;
            }

            BOOL bIsVerify = m_drvIO.VerifySector(m_u64CurrentSector, nHowManySectors);
            //if (g_bIsCancel) break;

            if (!bIsVerify)
            {
                //CLogWriter::Log("drvIO.VerifySector find Bad sector pos=%I64d.", m_u64CurrentSector);
            }

            {
                //AutoLock locker(getSectorInfoMutexLock);

                if (m_bQuick && (m_u64CurrentSector - m_u64StartSector) > minSectors)
                    m_u64CurrentSector += stepSectors;
                else
                    m_u64CurrentSector += nHowManySectors;

                if (!bIsVerify)
                {
                    m_vectBadSecs.push_back(m_u64CurrentSector - m_u64StartSector + nHowManySectors / 2);
                }
            }

            //检测用户是否点击了取消
            //if (g_bIsCancel)  break;
        }
        

    } while (0);
   
    pIter->Release();
    pIter = NULL;
    m_drvIO.CloseDisk();
    long long a = test;
	return iResult;
}




