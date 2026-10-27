#ifndef _YOLOV8_MJPEG_SERVER_H_
#define _YOLOV8_MJPEG_SERVER_H_

#include <stddef.h>
#include <opencv2/core/core.hpp>

class MjpegServer
{
public:
    MjpegServer();
    ~MjpegServer();

    bool start(int port);
    void publish(const cv::Mat &frame);
    void stop();

private:
    void acceptClient();
    void closeClient();
    bool sendAll(const void *data, size_t size);

    int listen_fd_;
    int client_fd_;
};

#endif // _YOLOV8_MJPEG_SERVER_H_