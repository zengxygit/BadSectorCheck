#include "pch.h"
#include "DiskCheckBase.h"
#include "../../DiskBackup/include/DeviceManagerModule.itf"
#include "../../DiskBackup/include/DeviceManager.itf"
#include "../../DiskBackup/include/Container.itf"
#include "../../DiskBackup/include/NTFSLib.h"
#include "../../DiskBackup/mod.Partition/PartitionEntry.itf"
#include "../../DiskBackup/include/DiskEntry.itf"
#include "../../DiskBackup/include/DiskGeometry.itf"
#include "../../DiskBackup/include/PartitionManager.itf"
#include "../../DiskBackup/include/FileSystemPartition.itf"
#include "../../DiskBackup/include/IVolumeEntry.h"

#define ONE_TIMES_TEST_SECTOR  (256 * 2)

// ─────────────────────────────────────────────────────────────────
// 打开设备管理器模块，获取 IManager*
// ─────────────────────────────────────────────────────────────────
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
        iResult = BP_ERR_CREATE_DEVICE_MANAGER;
    }

    if (pDevMgr == NULL)
        return iResult;

    apDevModule->SetCurrentDeviceManager(pDevMgr);
    *ppDevMgr = pDevMgr;
    return iResult;
}

// ─────────────────────────────────────────────────────────────────
// 枚举所有物理磁盘
// ─────────────────────────────────────────────────────────────────
std::vector<DiskCheckBase::DiskInfo> DiskCheckBase::EnumerateDisks()
{
    std::vector<DiskInfo> result;

    CMomPtr<IDeviceManagerModule> apModule = OpenModule(DeviceManager);
    if (apModule == NULL)
        return result;

    IManager* pMgr = nullptr;
    try { pMgr = apModule->CreateDeviceManager(); }
    catch (...) { return result; }
    if (!pMgr) return result;

    apModule->SetCurrentDeviceManager(pMgr);

    CMomPtr<IIterator> pIter = pMgr->NewIterator();
    if (!pIter) return result;

    IManagerItem* pItem = nullptr;
    while ((pItem = (IManagerItem*)pIter->Next()) != nullptr)
    {
        IDiskEntry*      pDisk  = pItem->GetInterface(IDiskEntry);
        IPartitionManager* pPM  = pItem->GetInterface(IPartitionManager);
        if (!pDisk || !pPM) continue;    // 只取磁盘级 item
        if (pDisk->IsVirtualDisk()) continue; // 与 EPM 一致：不展示虚拟盘

        IDiskGeometry* pGeom = pItem->GetInterface(IDiskGeometry);

        DiskInfo info;
        info.index         = (int)pDisk->GetIndex();
        info.name          = pDisk->GetSystemName() ? pDisk->GetSystemName() : "";
        info.startSector   = (int64_t)pDisk->GetStartSector();
        info.sectorCount   = (int64_t)pDisk->GetSectorCount();
        info.bytesPerSector = pGeom ? pGeom->GetBytesPerSector() : 512;

        // 枚举磁盘上的分区
        CMomPtr<IIterator> pPartItr = pPM->NewIterator();
        if (pPartItr)
        {
            IManagerItem* pPartItem = nullptr;
            while ((pPartItem = (IManagerItem*)pPartItr->Next()) != nullptr)
            {
                IPartitionEntry* pPartEntry = pPartItem->GetInterface(IPartitionEntry);
                if (!pPartEntry) continue;

                // 跳过空分区类型
                if (pPartEntry->GetPartitionType() == PART_Empty) continue;

                IFileSystemPartition* pFS  = pPartItem->GetInterface(IFileSystemPartition);
                IVolumeEntryInfo*     pVol = pPartItem->GetInterface(IVolumeEntryInfo);

                PartitionInfo pinfo;
                pinfo.diskIndex     = info.index;
                pinfo.systemName    = pPartEntry->GetSystemName() ? pPartEntry->GetSystemName() : "";
                pinfo.label         = (pFS && pFS->GetLabel()) ? pFS->GetLabel() : "";
                pinfo.driveLetter   = pVol ? pVol->GetDriveLetter() : 0;
                pinfo.startSector   = (int64_t)pPartEntry->GetStartSector();
                pinfo.sectorCount   = (int64_t)pPartEntry->GetSectorCount();
                pinfo.bytesPerSector = info.bytesPerSector;
                info.partitions.push_back(pinfo);
            }
        }

        result.push_back(info);
    }

    return result;   // apModule 析构时自动释放
}

// ─────────────────────────────────────────────────────────────────
// 线程安全地获取当前进度
// ─────────────────────────────────────────────────────────────────
void DiskCheckBase::GetProgress(int64_t& currentSector, std::vector<int64_t>& badSecs)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    currentSector = m_u64CurrentSector;
    badSecs       = m_vectBadSecs;
}

// ─────────────────────────────────────────────────────────────────
// 执行坏扇区检测（在子线程中调用）
// ─────────────────────────────────────────────────────────────────
TBRESULT DiskCheckBase::DoCheck(U32 uDiskIndex)
{
    // 防止重入
    if (m_bIsRunning)
        return BP_ERR_SUCCESS;

    // 重置运行状态
    m_bIsCancel = false;
    m_bIsRunning = true;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_vectBadSecs.clear();
    }

    TBRESULT iResult = BP_ERR_SUCCESS;
    CMomPtr<IIterator> pIter;

    do {
        iResult = OpenDevMgrModule(m_apDevModule, &m_pDevMgr);
        if (iResult != 0) break;

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

        // ── 按磁盘 index 查找对应的磁盘级 IManagerItem ──────────
        CMomPtr<IManagerItem> pDiskItem = NULL;
        IDiskEntry* pFoundDiskEntry     = NULL;

        IManagerItem* pItem = (IManagerItem*)pIter->Next();
        while (pItem != NULL)
        {
            IDiskEntry*       pDiskEntry = pItem->GetInterface(IDiskEntry);
            IPartitionManager* pPartMgr  = pItem->GetInterface(IPartitionManager);
            if (pDiskEntry && pPartMgr && pDiskEntry->GetIndex() == (U32)uDiskIndex)
            {
                pDiskItem        = pItem;
                pFoundDiskEntry  = pDiskEntry;
                break;
            }
            pItem = (IManagerItem*)pIter->Next();
        }

        if (pDiskItem == NULL || pFoundDiskEntry == NULL)
        {
            iResult = BP_ERR_INVALID_PARAMETER;
            break;
        }

        // ── 初始化 IO 设置 ──────────────────────────────────────
        CHardDriveIOSetting drvIoSetting(m_pDevMgr);
        CHardDriveIO::LoadSetting(drvIoSetting);

        // 与当前 DiskBackup 口径对齐：按 Entry 返回范围扫描
        m_u64StartSector  = pFoundDiskEntry->GetStartSector();
        m_u64SectorCount  = pFoundDiskEntry->GetSectorCount();

        if (m_u64StartSector != 0)
        {
            m_u64StartSector = 0;
			m_u64SectorCount = pFoundDiskEntry->GetStartSector() + pFoundDiskEntry->GetSectorCount() - 1;
        }

        // ── 打开物理磁盘 ─────────────────────────────────────────
        int nHarddiskIndex = (int)uDiskIndex;
        if (!m_drvIO.OpenDisk(nHarddiskIndex))
        {
            iResult = -1;
            break;
        }

        m_u32SectorSize = m_drvIO.GetSectorSize();
        if (m_u32SectorSize == 0)
        {
            iResult = -1;
            break;
        }

        // ── 快速模式步进参数 ─────────────────────────────────────
        const U64 stepSize    = 10ULL * 1024 * 1024;
        const U64 minStartSize = 2ULL * 1024 * 1024;
        U64 minSectors  = minStartSize / m_u32SectorSize;
        U64 stepSectors = stepSize     / m_u32SectorSize;

        // ── 扫描循环 ─────────────────────────────────────────────
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_u64CurrentSector = m_u64StartSector;
        }

        while (true)
        {
            int64_t curSector;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                curSector = m_u64CurrentSector;
            }
            if (curSector >= m_u64SectorCount + m_u64StartSector)
                break;
            if (m_bIsCancel)
            {
                iResult = BP_ERR_CANCELED_COMMAND;
                break;
            }

            S64 nHowManySectors = ONE_TIMES_TEST_SECTOR;
            if ((U64)nHowManySectors + curSector > m_u64SectorCount + m_u64StartSector)
                nHowManySectors = m_u64SectorCount + m_u64StartSector - curSector;

            BOOL bIsVerify = m_drvIO.VerifySector(curSector, nHowManySectors);

            if (m_bIsCancel)
            {
                iResult = BP_ERR_CANCELED_COMMAND;
                break;
            }

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (!bIsVerify)
                {
                    // 记录相对于 startSector 的坏扇区偏移（取检测段中点）
                    m_vectBadSecs.push_back(curSector - m_u64StartSector + nHowManySectors / 2);
                }

                if (m_bQuick && (curSector - m_u64StartSector) > (int64_t)minSectors)
                    m_u64CurrentSector = curSector + stepSectors;
                else
                    m_u64CurrentSector = curSector + nHowManySectors;
            }
        }

    } while (0);

    // ── 清理 ─────────────────────────────────────────────────────
    m_drvIO.CloseDisk();
    m_pDevMgr    = nullptr;
    m_apDevModule = NULL;
    m_bIsRunning  = false;
    return iResult;
}

// ─────────────────────────────────────────────────────────────────
// 枚举所有分区（跨磁盘平铺）
// ─────────────────────────────────────────────────────────────────
std::vector<DiskCheckBase::PartitionInfo> DiskCheckBase::EnumeratePartitions()
{
    std::vector<PartitionInfo> result;
    std::vector<DiskInfo> disks = EnumerateDisks();
    for (const auto& d : disks)
        for (const auto& p : d.partitions)
            result.push_back(p);
    return result;
}

// ─────────────────────────────────────────────────────────────────
// 执行坏扇区检测 —— 分区扫描
// ─────────────────────────────────────────────────────────────────
TBRESULT DiskCheckBase::DoCheckPartition(int diskIndex, int64_t startSector, int64_t sectorCount)
{
    if (m_bIsRunning)
        return BP_ERR_SUCCESS;

    m_bIsCancel  = false;
    m_bIsRunning = true;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_vectBadSecs.clear();
    }

    TBRESULT iResult = BP_ERR_SUCCESS;

    do {
        iResult = OpenDevMgrModule(m_apDevModule, &m_pDevMgr);
        if (iResult != 0) break;
        if (m_pDevMgr == NULL) { iResult = BP_ERR_CREATE_DEVICE_MANAGER; break; }

        CHardDriveIOSetting drvIoSetting(m_pDevMgr);
        CHardDriveIO::LoadSetting(drvIoSetting);

        // 直接使用传入的分区范围
        m_u64StartSector = (U64)startSector;
        m_u64SectorCount = (U64)sectorCount;

        if (!m_drvIO.OpenDisk(diskIndex)) { iResult = -1; break; }

        m_u32SectorSize = m_drvIO.GetSectorSize();
        if (m_u32SectorSize == 0) { iResult = -1; break; }

        const U64 stepSize     = 10ULL * 1024 * 1024;
        const U64 minStartSize =  2ULL * 1024 * 1024;
        U64 minSectors  = minStartSize / m_u32SectorSize;
        U64 stepSectors = stepSize     / m_u32SectorSize;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_u64CurrentSector = m_u64StartSector;
        }

        while (true)
        {
            int64_t curSector;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                curSector = m_u64CurrentSector;
            }
            if ((U64)curSector >= m_u64StartSector + m_u64SectorCount) break;
            if (m_bIsCancel) { iResult = BP_ERR_CANCELED_COMMAND; break; }

            S64 nHowManySectors = ONE_TIMES_TEST_SECTOR;
            if ((U64)nHowManySectors + curSector > m_u64StartSector + m_u64SectorCount)
                nHowManySectors = (S64)(m_u64StartSector + m_u64SectorCount - curSector);

            BOOL bIsVerify = m_drvIO.VerifySector(curSector, nHowManySectors);

            if (m_bIsCancel) { iResult = BP_ERR_CANCELED_COMMAND; break; }

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (!bIsVerify)
                {
                    // 坏扇区记录为相对于分区 startSector 的偏移
                    m_vectBadSecs.push_back(curSector - (int64_t)m_u64StartSector + nHowManySectors / 2);
                }

                U64 relPos = (U64)curSector - m_u64StartSector;
                if (m_bQuick && relPos > minSectors)
                    m_u64CurrentSector = curSector + stepSectors;
                else
                    m_u64CurrentSector = curSector + nHowManySectors;
            }
        }

    } while (0);

    m_drvIO.CloseDisk();
    m_pDevMgr     = nullptr;
    m_apDevModule = NULL;
    m_bIsRunning  = false;
    return iResult;
}

