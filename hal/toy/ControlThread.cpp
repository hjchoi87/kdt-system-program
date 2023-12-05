#include <iostream>
#include <cstdio>
#include <unistd.h>

#include "ControlThread.h"

using std::cout;
using std::endl;

ControlThread::ControlThread()
{
    cout << "TOY: 여기는 C++ 영역 입니다" << endl;
}

ControlThread::~ControlThread()
{
    cout << "TOY: 소멸자입니다." << endl;
}

int ControlThread::takePicture()
{
    cout << "TOY: 사진을 캡쳐합니다." << endl;

    return 0;
}

int ControlThread::dump()
{
    cout << "OEM: 카메라 덤프." << endl;

    return 0;
}
