#ifndef _CONTROL_THREAD_H_
#define _CONTROL_THREAD_H_

class ControlThread {

public:
    ControlThread();
    ~ControlThread();

public:
    // 사진 찍는 메소드
    int takePicture();
    int dump();

private:

}; // class ControlThread

#endif /* _CONTROL_THREAD_H_ */
