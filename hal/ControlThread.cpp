#include <iostream>
#include <cstdio>
#include <unistd.h>

#include "ControlThread.h"

using std::cout;
using std::endl;

ControlThread::ControlThread()
{
    cout << "C++ 연동:  여기는 C++ 영역 입니다" << endl;
}

ControlThread::~ControlThread()
{
    cout << "C++ 연동:  소멸자입니다." << endl;
}

int ControlThread::takePicture()
{
    cout << "C++ 연동:  사진을 캡쳐합니다." << endl;

    return 0;
}
