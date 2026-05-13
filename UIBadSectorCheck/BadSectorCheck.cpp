#ifndef BP_ERR_MODE
#define BP_ERR_MODE  TB_MOD_BadSectorCheck
#endif

// UIBadSectorCheck 是 DLL 的使用方，不定义 BADSECTORCHECKDLL_EXPORTS
// DiskCheckBase.h 中的 BADSECTORCHECK_API 将展开为 __declspec(dllimport)

#include "BadSectorCheck.h"
#include "windows.h"
#include "D:/DC-main/Include/ModuleDef.h"
#include "D:/DC-main/Include/ErrorCode.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QSizePolicy>
#include <QMessageBox>
#include <QDebug>
#include <ctime>
#include <algorithm>

#include "../BadSectorCheckDll/DiskCheckBase.h"

// ═══════════════════════════════════════════════════════════════════
// SectorGridWidget
// ═══════════════════════════════════════════════════════════════════
SectorGridWidget::SectorGridWidget(int rows, int cols, QWidget* parent)
    : QWidget(parent), m_rows(rows), m_cols(cols)
{
    m_cells.fill(NotTested, rows * cols);
    setFixedSize(cols * CELL_PX + 1, rows * CELL_PX + 1);
}

void SectorGridWidget::updateCells(const QVector<int>& cells)
{
    m_cells = cells;
    update();   // 触发 paintEvent
}

void SectorGridWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);

    static const QColor clrNotTested(0xEF, 0xEF, 0xEF);
    static const QColor clrGood     (0x4C, 0xAF, 0x50);
    static const QColor clrBad      (0xF4, 0x43, 0x36);
    static const QColor clrBorder   (0xCD, 0xD6, 0xDF);

    p.setPen(clrBorder);

    for (int r = 0; r < m_rows; ++r)
    {
        for (int c = 0; c < m_cols; ++c)
        {
            int idx = r * m_cols + c;
            CellState st = (idx < m_cells.size())
                            ? (CellState)m_cells[idx]
                            : NotTested;

            QColor fill;
            switch (st) {
                case Good: fill = clrGood; break;
                case Bad:  fill = clrBad;  break;
                default:   fill = clrNotTested; break;
            }

            QRect rc(c * CELL_PX, r * CELL_PX, CELL_PX, CELL_PX);
            p.fillRect(rc.adjusted(1,1,0,0), fill);
            p.drawRect(rc);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════
// DiskCheckThread
// ═══════════════════════════════════════════════════════════════════
DiskCheckThread::DiskCheckThread(DiskCheckBase* pCheck, int diskIndex, QObject* parent)
    : QThread(parent), m_pCheck(pCheck), m_diskIndex(diskIndex)
{
}

DiskCheckThread::DiskCheckThread(DiskCheckBase* pCheck, int diskIndex,
                                 int64_t startSector, int64_t sectorCount,
                                 QObject* parent)
    : QThread(parent), m_pCheck(pCheck), m_diskIndex(diskIndex),
      m_bPartitionMode(true), m_startSector(startSector), m_sectorCount(sectorCount)
{
}

void DiskCheckThread::run()
{
    int result;
    if (m_bPartitionMode)
        result = m_pCheck->DoCheckPartition(m_diskIndex, m_startSector, m_sectorCount);
    else
        result = m_pCheck->DoCheck((U32)m_diskIndex);
    emit checkFinished(result);
}

// ═══════════════════════════════════════════════════════════════════
// BadSectorCheck — 构造 / 析构
// ═══════════════════════════════════════════════════════════════════
BadSectorCheck::BadSectorCheck(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("Bad Sector Check"));
    setMinimumSize(920, 620);

    m_gridCells.fill(SectorGridWidget::NotTested, ROWS * COLS);

    connect(&m_timer, &QTimer::timeout, this, &BadSectorCheck::onTimer);

    buildUI();
    populateDiskList();
}

BadSectorCheck::~BadSectorCheck()
{
    // 确保子线程安全退出
    if (m_pDiskCheck && m_pDiskCheck->IsRunning())
    {
        m_pDiskCheck->Cancel();
        if (m_pCheckThread)
            m_pCheckThread->wait(5000);
    }
    delete m_pDiskCheck;
}

// ═══════════════════════════════════════════════════════════════════
// 构建 UI 布局
// ═══════════════════════════════════════════════════════════════════
void BadSectorCheck::buildUI()
{
    QWidget* central = new QWidget(this);
    setCentralWidget(central);

    // ── 左侧：格子 + 图例 ────────────────────────────────────────
    m_pGrid = new SectorGridWidget(ROWS, COLS, central);

    // 图例行
    QWidget* legendRow = new QWidget(central);
    {
        QHBoxLayout* lh = new QHBoxLayout(legendRow);
        lh->setContentsMargins(0, 4, 0, 0);
        lh->setSpacing(16);

        auto makeLegendItem = [&](const QColor& color, const QString& text) {
            QWidget* item = new QWidget(legendRow);
            QHBoxLayout* ih = new QHBoxLayout(item);
            ih->setContentsMargins(0,0,0,0); ih->setSpacing(6);

            QLabel* box = new QLabel(item);
            box->setFixedSize(12, 12);
            QString style = QString("background:%1; border:1px solid #555;").arg(color.name());
            box->setStyleSheet(style);

            QLabel* lbl = new QLabel(text, item);
            lbl->setStyleSheet("color:#333; font-size:12px;");

            ih->addWidget(box);
            ih->addWidget(lbl);
            return item;
        };

        lh->addWidget(makeLegendItem(QColor(0xAA,0xAA,0xAA), tr("Not tested")));
        lh->addWidget(makeLegendItem(QColor(0x4C,0xAF,0x50), tr("Good sector")));
        lh->addWidget(makeLegendItem(QColor(0xF4,0x43,0x36), tr("Bad sector")));
        lh->addStretch();
    }

    QVBoxLayout* leftVBox = new QVBoxLayout();
    leftVBox->setContentsMargins(0,0,0,0);
    leftVBox->setSpacing(0);
    leftVBox->addWidget(m_pGrid);
    leftVBox->addWidget(legendRow);
    leftVBox->addStretch();

    // ── 右侧：控制面板 ───────────────────────────────────────────
    QVBoxLayout* rightVBox = new QVBoxLayout();
    rightVBox->setContentsMargins(0, 0, 0, 0);
    rightVBox->setSpacing(10);

    // 目标选择（单选决定该下拉是磁盘列表还是分区列表）
    QLabel* diskLbl = new QLabel(tr("Target:"), central);
    diskLbl->setStyleSheet("font-size:13px;");

    m_pDiskCombo = new QComboBox(central);
    m_pDiskCombo->setFixedWidth(210);
    m_pDiskCombo->setStyleSheet("font-size:12px;");

    // 快速检测
    m_pQuickCheck = new QCheckBox(tr("Perform a quick check process"), central);
    m_pQuickCheck->setChecked(true);
    m_pQuickCheck->setStyleSheet("font-size:12px;");

    // 磁盘名称（系统路径）
    m_pDiskName = new QLabel(central);
    m_pDiskName->setStyleSheet("color:#444; font-size:12px;");
    m_pDiskName->setWordWrap(true);

    // 统计标签
    auto makeStatLabel = [&](const QString& text) -> QLabel* {
        QLabel* lbl = new QLabel(text, central);
        lbl->setStyleSheet("font-size:13px; color:#222;");
        return lbl;
    };

    m_pTotal     = makeStatLabel(tr("Total sectors:   -"));
    m_pBad       = makeStatLabel(tr("Bad sectors:     -"));
    m_pCurrent   = makeStatLabel(tr("Current sector:  -"));
    m_pElapsed   = makeStatLabel(tr("Elapsed:         -"));
    m_pRemaining = makeStatLabel(tr("Remaining:       -"));
    m_pSpeed     = makeStatLabel(tr("Speed:           -"));

    // 按钮
    m_pStartBtn  = new QPushButton(tr("Start"),  central);
    m_pStopBtn   = new QPushButton(tr("Stop"),   central);
    for (auto* btn : {m_pStartBtn, m_pStopBtn})
    {
        btn->setFixedSize(90, 28);
        btn->setStyleSheet(
            "QPushButton{background:#1976D2;color:white;border-radius:4px;font-size:13px;}"
            "QPushButton:hover{background:#1565C0;}"
            "QPushButton:disabled{background:#9E9E9E;}");
    }
    m_pStopBtn->hide();

    connect(m_pStartBtn,  &QPushButton::clicked, this, &BadSectorCheck::onStartClicked);
    connect(m_pStopBtn,   &QPushButton::clicked, this, &BadSectorCheck::onStopClicked);

    m_pScanDisk = new QRadioButton(tr("Scan Disk"), central);
    m_pScanPartition = new QRadioButton(tr("Scan Partition"), central);
    m_pScanDisk->setChecked(true);

    connect(m_pScanDisk, &QRadioButton::toggled, this, [this](bool) {
        refreshTargetCombo();
        resetGrid();
    });
    connect(m_pScanPartition, &QRadioButton::toggled, this, [this](bool) {
        refreshTargetCombo();
        resetGrid();
    });

    // 单一下拉框切换项时更新描述
    connect(m_pDiskCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
                Q_UNUSED(idx);
                m_pDiskName->setText(m_pDiskCombo->itemData(m_pDiskCombo->currentIndex(), Qt::UserRole + 1).toString());
                resetGrid();
            });

    // 组装右侧布局
    // 单选行（Scan Disk / Scan Partition）
    {
        QHBoxLayout* modeRow = new QHBoxLayout();
        modeRow->setContentsMargins(0, 0, 0, 0);
        modeRow->setSpacing(12);
        modeRow->addWidget(m_pScanDisk);
        modeRow->addWidget(m_pScanPartition);
        modeRow->addStretch();
        rightVBox->addLayout(modeRow);
    }

    rightVBox->addWidget(diskLbl);
    rightVBox->addWidget(m_pDiskCombo);
    rightVBox->addWidget(m_pQuickCheck);
    rightVBox->addSpacing(8);
    rightVBox->addWidget(m_pDiskName);
    rightVBox->addSpacing(16);
    rightVBox->addWidget(m_pTotal);
    rightVBox->addWidget(m_pBad);
    rightVBox->addWidget(m_pCurrent);
    rightVBox->addSpacing(8);
    rightVBox->addWidget(m_pElapsed);
    rightVBox->addWidget(m_pRemaining);
    rightVBox->addWidget(m_pSpeed);
    rightVBox->addStretch();
    rightVBox->addWidget(m_pStartBtn,  0, Qt::AlignRight);
    rightVBox->addWidget(m_pStopBtn,   0, Qt::AlignRight);

    // ── 主水平布局 ────────────────────────────────────────────────
    QHBoxLayout* mainH = new QHBoxLayout(central);
    mainH->setContentsMargins(16, 16, 16, 16);
    mainH->setSpacing(16);
    mainH->addLayout(leftVBox);
    mainH->addLayout(rightVBox);
}

// ═══════════════════════════════════════════════════════════════════
// 根据单选模式刷新“唯一目标下拉框”
// ═══════════════════════════════════════════════════════════════════
void BadSectorCheck::refreshTargetCombo()
{
    m_pDiskCombo->clear();

    if (m_pScanPartition && m_pScanPartition->isChecked())
    {
        std::vector<DiskCheckBase::PartitionInfo> parts = DiskCheckBase::EnumeratePartitions();

        // ① 只保留有盘符的分区（与 EPM 一致：跳过 EFI/MSR/恢复等无盘符分区）
        struct PartItem {
            QString  text;
            DiskCheckBase::PartitionInfo info;
        };
        std::vector<PartItem> items;
        for (const auto& p : parts)
        {
            if (p.driveLetter == 0) continue;   // 无盘符，跳过

            int64_t partBytes = (int64_t)p.sectorCount * p.bytesPerSector;
            QString text = QString("%1: (%2)").arg(QChar(p.driveLetter))
                                              .arg(formatBytes(partBytes));
            items.push_back({ text, p });
        }

        // ② 按盘符字母序排序（与 EPM 气泡排序逻辑等价）
        std::sort(items.begin(), items.end(), [](const PartItem& a, const PartItem& b){
            return a.info.driveLetter < b.info.driveLetter;
        });

        for (const auto& item : items)
        {
            m_pDiskCombo->addItem(item.text);
            int idx = m_pDiskCombo->count() - 1;
            m_pDiskCombo->setItemData(idx, 1,                                    Qt::UserRole);
            m_pDiskCombo->setItemData(idx, QString::fromStdString(item.info.systemName), Qt::UserRole + 1);
            m_pDiskCombo->setItemData(idx, item.info.diskIndex,                  Qt::UserRole + 2);
            m_pDiskCombo->setItemData(idx, (qlonglong)item.info.startSector,     Qt::UserRole + 3);
            m_pDiskCombo->setItemData(idx, (qlonglong)item.info.sectorCount,     Qt::UserRole + 4);
        }
    }
    else
    {
        std::vector<DiskCheckBase::DiskInfo> disks = DiskCheckBase::EnumerateDisks();
        for (const auto& d : disks)
        {
            int64_t bytes = (d.startSector + d.sectorCount) * (int64_t)d.bytesPerSector;
            QString text = QString("Disk %1 (%2)").arg(d.index).arg(formatBytes(bytes));
            m_pDiskCombo->addItem(text);
            int idx = m_pDiskCombo->count() - 1;
            // UserRole:   type (0=disk)
            // UserRole+1: 描述文本
            // UserRole+2: diskIndex
            m_pDiskCombo->setItemData(idx, 0, Qt::UserRole);
            m_pDiskCombo->setItemData(idx, QString::fromStdString(d.name), Qt::UserRole + 1);
            m_pDiskCombo->setItemData(idx, d.index, Qt::UserRole + 2);
        }
    }

    if (m_pDiskCombo->count() > 0)
        m_pDiskName->setText(m_pDiskCombo->itemData(0, Qt::UserRole + 1).toString());
    else
        m_pDiskName->setText("-");
}

// ═══════════════════════════════════════════════════════════════════
// 初始化目标列表
// ═══════════════════════════════════════════════════════════════════
void BadSectorCheck::populateDiskList()
{
    refreshTargetCombo();
    m_pStartBtn->setEnabled(m_pDiskCombo->count() > 0);
}

// ═══════════════════════════════════════════════════════════════════
// 重置格子状态
// ═══════════════════════════════════════════════════════════════════
void BadSectorCheck::resetGrid()
{
    m_gridCells.fill(SectorGridWidget::NotTested, ROWS * COLS);
    m_pGrid->updateCells(m_gridCells);
    m_badSecs.clear();
    m_i64CurrentSecs = 0;
    m_i64TotalSecs   = 0;

    m_pTotal->setText(tr("Total sectors:   -"));
    m_pBad->setText(tr("Bad sectors:     -"));
    m_pCurrent->setText(tr("Current sector:  -"));
    m_pElapsed->setText(tr("Elapsed:         -"));
    m_pRemaining->setText(tr("Remaining:       -"));
    m_pSpeed->setText(tr("Speed:           -"));
}

// ═══════════════════════════════════════════════════════════════════
// Start 按钮
// ═══════════════════════════════════════════════════════════════════
void BadSectorCheck::onStartClicked()
{
    resetGrid();

    delete m_pDiskCheck;
    m_pDiskCheck = new DiskCheckBase();
    m_pDiskCheck->SetQuick(m_pQuickCheck->isChecked());

    int comboIndex = m_pDiskCombo->currentIndex();
    if (comboIndex < 0) return;

    int targetType = m_pDiskCombo->itemData(comboIndex, Qt::UserRole).toInt();
    if (targetType == 1)
    {
        int diskIndex = m_pDiskCombo->itemData(comboIndex, Qt::UserRole + 2).toInt();
        int64_t startSector = m_pDiskCombo->itemData(comboIndex, Qt::UserRole + 3).toLongLong();
        int64_t sectorCount = m_pDiskCombo->itemData(comboIndex, Qt::UserRole + 4).toLongLong();
        if (diskIndex < 0 || sectorCount <= 0) return;
        m_pCheckThread = new DiskCheckThread(m_pDiskCheck, diskIndex, startSector, sectorCount, this);
    }
    else
    {
        int diskIndex = m_pDiskCombo->itemData(comboIndex, Qt::UserRole + 2).toInt();
        if (diskIndex < 0) return;
        m_pCheckThread = new DiskCheckThread(m_pDiskCheck, diskIndex, this);
    }

    connect(m_pCheckThread, &DiskCheckThread::checkFinished,
            this, &BadSectorCheck::onCheckFinished);
    connect(m_pCheckThread, &QThread::finished,
            m_pCheckThread, &QObject::deleteLater);

    m_tStartTime = time(nullptr);
    m_pCheckThread->start();
    m_timer.start(UPDATE_INTERVAL);

    m_pStartBtn->hide();
    m_pStopBtn->show();
    m_pDiskCombo->setEnabled(false);
    if (m_pScanDisk)       m_pScanDisk->setEnabled(false);
    if (m_pScanPartition)  m_pScanPartition->setEnabled(false);
    m_pQuickCheck->setEnabled(false);
}

// ═══════════════════════════════════════════════════════════════════
// Stop 按钮
// ═══════════════════════════════════════════════════════════════════
void BadSectorCheck::onStopClicked()
{
    m_timer.stop();

    if (m_pDiskCheck)
        m_pDiskCheck->Cancel();

    // 等待线程结束（最多 5 秒，期间保持事件循环响应）
    if (m_pCheckThread)
    {
        m_pCheckThread->wait(5000);
        m_pCheckThread = nullptr;
    }

    updateStats();

    m_pStopBtn->hide();
    m_pStartBtn->show();
    m_pDiskCombo->setEnabled(true);
    if (m_pScanDisk)      m_pScanDisk->setEnabled(true);
    if (m_pScanPartition) m_pScanPartition->setEnabled(true);
    m_pQuickCheck->setEnabled(true);
}

// ═══════════════════════════════════════════════════════════════════
// Finish 按钮
// ═══════════════════════════════════════════════════════════════════
void BadSectorCheck::onFinishClicked()
{
    close();
}

// ═══════════════════════════════════════════════════════════════════
// 定时器：刷新进度
// ═══════════════════════════════════════════════════════════════════
void BadSectorCheck::onTimer()
{
    if (!m_pDiskCheck) return;

    // 从检测线程拉取进度
    int64_t absCurrent;
    std::vector<int64_t> badSecs;
    m_pDiskCheck->GetProgress(absCurrent, badSecs);

    m_badSecs        = badSecs;
    m_i64TotalSecs   = m_pDiskCheck->GetSectorCount();
    m_u32SectorSize  = m_pDiskCheck->GetSectorSize();
    m_i64CurrentSecs = absCurrent - m_pDiskCheck->GetStartSector();

    // 每格对应扇区数
    if (m_i64TotalSecs > 0)
        m_dSecsPerSpace = (double)m_i64TotalSecs / (double)(ROWS * COLS);

    // ── 更新格子状态 ─────────────────────────────────────────────
    const int total = ROWS * COLS;
    for (int i = 0; i < total; ++i)
    {
        int64_t i64SecPos = (int64_t)((double)i * m_dSecsPerSpace);

        if (isBadSector(i64SecPos) || m_gridCells[i] == SectorGridWidget::Bad)
            m_gridCells[i] = SectorGridWidget::Bad;
        else
            m_gridCells[i] = (i64SecPos < m_i64CurrentSecs)
                              ? SectorGridWidget::Good
                              : SectorGridWidget::NotTested;
    }
    m_pGrid->updateCells(m_gridCells);

    updateStats();
}

// ═══════════════════════════════════════════════════════════════════
// 检测线程结束
// ═══════════════════════════════════════════════════════════════════
void BadSectorCheck::onCheckFinished(int result)
{
    m_timer.stop();
    m_pCheckThread = nullptr;

    // 做一次最终刷新，确保格子显示完成状态
    onTimer();

    m_pStopBtn->hide();
    m_pStartBtn->show();

    // Stop 触发的取消不弹错误；其他失败才提示
    if (result != BP_ERR_SUCCESS && result != BP_ERR_CANCELED_COMMAND)
    {
        QMessageBox::warning(this, tr("Bad Sector Check"),
            tr("Check failed, error code: %1").arg(result));
    }

    m_pDiskCombo->setEnabled(true);
    if (m_pScanDisk)      m_pScanDisk->setEnabled(true);
    if (m_pScanPartition) m_pScanPartition->setEnabled(true);
    m_pQuickCheck->setEnabled(true);

    qDebug() << "DoCheck finished, result=" << result
             << " bad sectors=" << (int)m_badSecs.size();
}

// ═══════════════════════════════════════════════════════════════════
// 刷新右侧统计标签  （与 SurfaceTestLogic::slotTimeout 逻辑对应）
// ═══════════════════════════════════════════════════════════════════
void BadSectorCheck::updateStats()
{
    m_pTotal->setText(tr("Total sectors:   %1").arg(m_i64TotalSecs));
    m_pBad->setText(  tr("Bad sectors:     %1").arg((int64_t)m_badSecs.size()));
    m_pCurrent->setText(tr("Current sector:  %1").arg(m_i64CurrentSecs));

    time_t elapsed = time(nullptr) - m_tStartTime;
    m_pElapsed->setText(tr("Elapsed:         %1").arg(formatTime(elapsed)));

    // 估算剩余时间
    if (m_i64TotalSecs > 0 && m_i64CurrentSecs > 0)
    {
        double rate = (double)m_i64CurrentSecs / (double)m_i64TotalSecs;
        rate = qMax(rate, 0.000001);
        time_t elapsedSafe = (elapsed < 1) ? 1 : elapsed;
        time_t remaining = (time_t)((double)elapsedSafe / rate * (1.0 - rate));
        m_pRemaining->setText(tr("Remaining:       %1").arg(formatTime(remaining)));

        // 速度
        if (!m_pQuickCheck->isChecked() && elapsed > 0)
        {
            int64_t bytesPerSec = (int64_t)m_u32SectorSize * m_i64CurrentSecs / elapsedSafe;
            m_pSpeed->setText(tr("Speed:           %1/s").arg(formatBytes(bytesPerSec)));
        }
        else
        {
            m_pSpeed->setText(tr("Speed:           -"));
        }
    }
}

// ═══════════════════════════════════════════════════════════════════
// 判断 relSecPos 是否落在某个坏扇区所在格范围内
// ═══════════════════════════════════════════════════════════════════
bool BadSectorCheck::isBadSector(int64_t relSecPos) const
{
    for (int64_t sec : m_badSecs)
    {
        if (sec >= relSecPos && sec < relSecPos + (int64_t)m_dSecsPerSpace + 1)
            return true;
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════════
// 辅助：格式化时间
// ═══════════════════════════════════════════════════════════════════
QString BadSectorCheck::formatTime(time_t seconds) const
{
    time_t h = seconds / 3600;
    time_t m = (seconds % 3600) / 60;
    time_t s = seconds % 60;
    return QString("%1:%2:%3")
        .arg((ulong)h, 2, 10, QChar('0'))
        .arg((ulong)m, 2, 10, QChar('0'))
        .arg((ulong)s, 2, 10, QChar('0'));
}

// ═══════════════════════════════════════════════════════════════════
// 辅助：格式化字节数
// ═══════════════════════════════════════════════════════════════════
QString BadSectorCheck::formatBytes(int64_t bytes) const
{
    if (bytes <= 0) return "0 B";
    const char* units[] = { "B","KB","MB","GB","TB" };
    int i = 0;
    double v = (double)bytes;
    while (v >= 1024.0 && i < 4) { v /= 1024.0; ++i; }
    return QString("%1 %2").arg(v, 0, 'f', 1).arg(units[i]);
}


