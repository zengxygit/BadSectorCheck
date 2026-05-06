#ifndef BP_ERR_MODE
#define BP_ERR_MODE  TB_MOD_BadSectorCheck
#endif

#include "BadSectorCheck.h"

// 添加要在此处预编译的标头
#include "windows.h"
#include "D:/DC-main/Include/ModuleDef.h"


#include "D:/DC-main/Include/ErrorCode.h"


#include "../BadSectorCheckDll/DiskCheckBase.h"

BadSectorCheck::BadSectorCheck(QWidget *parent)
    : QMainWindow(parent)
{
    DiskCheckBase* pDiskCheck = new DiskCheckBase;
    int iResult = pDiskCheck->DoCheck();
}

BadSectorCheck::~BadSectorCheck()
{


}

