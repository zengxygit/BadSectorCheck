#pragma once

#include <QtWidgets/QMainWindow>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QThread>
#include <QWidget>
#include <QPainter>
#include <ctime>
#include <vector>
#include <QRadioButton>

class DiskCheckBase;
// ─────────────────────────────────────────────────────────────────────
// 扇区状态格子绘制控件  (55列 × 45行 = 2475 格)
// ─────────────────────────────────────────────────────────────────────
class SectorGridWidget : public QWidget
{
    Q_OBJECT
public:
    enum CellState { NotTested = 0, Good = 1, Bad = 2 };

    static const int CELL_PX = 12;    // 每格像素大小

    explicit SectorGridWidget(int rows, int cols, QWidget* parent = nullptr);

    // 用新的格子状态数组重绘（长度必须 == rows*cols）
    void updateCells(const QVector<int>& cells);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    int          m_rows;
    int          m_cols;
    QVector<int> m_cells;   // CellState 值的列表
};

// ─────────────────────────────────────────────────────────────────────
// 磁盘检测工作线程
// ─────────────────────────────────────────────────────────────────────
class DiskCheckThread : public QThread
{
    Q_OBJECT
public:
    // 整盘模式
    explicit DiskCheckThread(DiskCheckBase* pCheck, int diskIndex, QObject* parent = nullptr);
    // 分区模式
    explicit DiskCheckThread(DiskCheckBase* pCheck, int diskIndex,
                             int64_t startSector, int64_t sectorCount,
                             QObject* parent = nullptr);
    void run() override;

signals:
    void checkFinished(int result);

private:
    DiskCheckBase* m_pCheck;
    int            m_diskIndex;
    bool           m_bPartitionMode = false;
    int64_t        m_startSector    = 0;
    int64_t        m_sectorCount    = 0;
};

// ─────────────────────────────────────────────────────────────────────
// 主窗口
// ─────────────────────────────────────────────────────────────────────
class BadSectorCheck : public QMainWindow
{
    Q_OBJECT

public:
    explicit BadSectorCheck(QWidget* parent = nullptr);
    ~BadSectorCheck();

private slots:
    void onStartClicked();
    void onStopClicked();
    void onFinishClicked();
    void onTimer();
    void onCheckFinished(int result);

private:
    void buildUI();
    void populateDiskList();
    void refreshTargetCombo();
    void resetGrid();
    void updateStats();
    bool isBadSector(int64_t relSecPos) const;
    QString formatTime(time_t seconds) const;
    QString formatBytes(int64_t bytes) const;

    static const int ROWS           = 45;
    static const int COLS           = 55;
    static const int UPDATE_INTERVAL = 500;   // ms

    // ── UI 控件 ──────────────────────────────────────────────────
    SectorGridWidget* m_pGrid       = nullptr;
    QLabel*           m_pLegend     = nullptr;   // 下方图例行（用 QHBoxLayout 另建）

    QComboBox*        m_pDiskCombo  = nullptr;
    QCheckBox*        m_pQuickCheck = nullptr;
    QLabel*           m_pDiskName   = nullptr;

    QLabel*           m_pTotal      = nullptr;
    QLabel*           m_pBad        = nullptr;
    QLabel*           m_pCurrent    = nullptr;
    QLabel*           m_pElapsed    = nullptr;
    QLabel*           m_pRemaining  = nullptr;
    QLabel*           m_pSpeed      = nullptr;

    QPushButton*      m_pStartBtn   = nullptr;
    QPushButton*      m_pStopBtn    = nullptr;
    QRadioButton*     m_pScanDisk   = nullptr;
	QRadioButton*     m_pScanPartition = nullptr;

    // ── 逻辑 ─────────────────────────────────────────────────────
    DiskCheckBase*   m_pDiskCheck   = nullptr;
    DiskCheckThread* m_pCheckThread = nullptr;
    QTimer           m_timer;

    QVector<int>           m_gridCells;  // 格子状态
    std::vector<int64_t>   m_badSecs;    // 累计坏扇区（相对偏移）
    int64_t  m_i64CurrentSecs = 0;       // 当前已扫描扇区数（相对）
    int64_t  m_i64TotalSecs   = 0;       // 本次扫描总扇区数
    double   m_dSecsPerSpace  = 1.0;     // 每格对应扇区数
    time_t   m_tStartTime     = 0;
    uint32_t m_u32SectorSize  = 512;
};


