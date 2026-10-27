#include "mjpeg_server.h"

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <opencv2/highgui/highgui.hpp>

#include <vector>

namespace
{
const char *kStreamHeaders =
    "HTTP/1.1 200 OK\r\n"
    "Cache-Control: no-cache, no-store, must-revalidate\r\n"
    "Pragma: no-cache\r\n"
    "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
    "Access-Control-Allow-Origin: *\r\n\r\n";
}

MjpegServer::MjpegServer() : listen_fd_(-1), client_fd_(-1)
{
}

MjpegServer::~MjpegServer()
{
    stop();
}

bool MjpegServer::start(int port)
{
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0)
    {
        perror("MJPEG socket");
        return false;
    }

    int reuse_address = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse_address, sizeof(reuse_address));

    sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(static_cast<unsigned short>(port));

    if (bind(listen_fd_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0)
    {
        perror("MJPEG bind");
        stop();
        return false;
    }

    if (listen(listen_fd_, 1) < 0)
    {
        perror("MJPEG listen");
        stop();
        return false;
    }

    int flags = fcntl(listen_fd_, F_GETFL, 0);
    if (flags < 0 || fcntl(listen_fd_, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        perror("MJPEG fcntl");
        stop();
        return false;
    }

    printf("MJPEG stream listening on port %d (open http://<board-ip>:%d/stream)\n", port, port);
    return true;
}

void MjpegServer::acceptClient()
{
    sockaddr_in client_address;
    socklen_t address_size = sizeof(client_address);
    int new_client = accept(listen_fd_, reinterpret_cast<sockaddr *>(&client_address), &address_size);
    if (new_client < 0)
    {
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
        {
            perror("MJPEG accept");
        }
        return;
    }

    // Поддерживается один зритель; новое подключение заменяет предыдущее.
    closeClient();
    client_fd_ = new_client;

    // Не задерживаем цикл инференса надолго, если клиент перестал принимать данные.
    timeval send_timeout;
    send_timeout.tv_sec = 0;
    send_timeout.tv_usec = 250000;
    setsockopt(client_fd_, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout));

    if (!sendAll(kStreamHeaders, strlen(kStreamHeaders)))
    {
        closeClient();
        return;
    }

    printf("MJPEG client connected\n");
}

bool MjpegServer::sendAll(const void *data, size_t size)
{
    const char *bytes = static_cast<const char *>(data);
    size_t sent = 0;
    while (sent < size)
    {
        ssize_t result = send(client_fd_, bytes + sent, size - sent, MSG_NOSIGNAL);
        if (result < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return false;
        }
        if (result == 0)
        {
            return false;
        }
        sent += static_cast<size_t>(result);
    }
    return true;
}

void MjpegServer::publish(const cv::Mat &frame)
{
    if (listen_fd_ < 0)
    {
        return;
    }

    acceptClient();
    if (client_fd_ < 0 || frame.empty())
    {
        return;
    }

    std::vector<unsigned char> jpeg;
    std::vector<int> jpeg_params;
    jpeg_params.push_back(cv::IMWRITE_JPEG_QUALITY);
    jpeg_params.push_back(75);
    if (!cv::imencode(".jpg", frame, jpeg, jpeg_params))
    {
        fprintf(stderr, "Failed to encode frame as JPEG\n");
        return;
    }

    char part_header[128];
    int header_size = snprintf(part_header, sizeof(part_header),
                               "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n",
                               jpeg.size());

    if (header_size <= 0 || static_cast<size_t>(header_size) >= sizeof(part_header) ||
        !sendAll(part_header, static_cast<size_t>(header_size)) ||
        !sendAll(jpeg.data(), jpeg.size()) ||
        !sendAll("\r\n", 2))
    {
        printf("MJPEG client disconnected or too slow\n");
        closeClient();
    }
}

void MjpegServer::closeClient()
{
    if (client_fd_ >= 0)
    {
        shutdown(client_fd_, SHUT_RDWR);
        close(client_fd_);
        client_fd_ = -1;
    }
}

void MjpegServer::stop()
{
    closeClient();
    if (listen_fd_ >= 0)
    {
        close(listen_fd_);
        listen_fd_ = -1;
    }
}