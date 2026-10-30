#include "mjpeg_server.h"   // Заголовочный файл с объявлением класса MjpegServer

// Стандартные POSIX-заголовки для работы с сокетами, ошибками и низкоуровневыми вызовами
#include <errno.h>           // Коды ошибок системных вызовов (errno)
#include <fcntl.h>          // Управление флагами файлов/сокетов (fcntl, O_NONBLOCK)
#include <netinet/in.h>     // Сетевые структуры (sockaddr_in, INADDR_ANY, htons и т.д.)
#include <stdio.h>          // printf, perror, fprintf — вывод сообщений и диагностика
#include <string.h>         // memset — инициализация структур нулями
#include <sys/socket.h>     // Базовые сокетные API (socket, bind, listen, accept, send и др.)
#include <sys/time.h>       // timeval — для установки таймаутов на сокете (SO_SNDTIMEO)
#include <unistd.h>         // close, shutdown — управление дескрипторами

// OpenCV: highgui нужен для imencode (кодирование кадра в JPEG)
#include <opencv2/highgui/highgui.hpp>

#include <vector>           // std::vector — динамические массивы для хранения JPEG-данных

// Пространство имён без имени: скрывает kStreamHeaders от внешней видимости
namespace
{
    // HTTP-заголовки для MJPEG-потока (multipart/x-mixed-replace)
    // boundary=frame — разделитель между JPEG-кадрами в потоке
    const char *kStreamHeaders =
        "HTTP/1.1 200 OK\r\n"
        "Cache-Control: no-cache, no-store, must-revalidate\r\n"
        "Pragma: no-cache\r\n"
        "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
        "Access-Control-Allow-Origin: *\r\n\r\n";
}

MjpegServer::MjpegServer() : listen_fd_(-1), client_fd_(-1)
{
    // Конструктор: инициализируем дескрипторы как «неактивные» (-1)
}

MjpegServer::~MjpegServer()
{
    // Деструктор: корректно останавливаем сервер и освобождаем ресурсы
    stop();
}

bool MjpegServer::start(int port)
{
    // Создаём TCP-сокет (IPv4, потоковый)
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0)
    {
        perror("MJPEG socket");
        return false;
    }

    // Разрешаем повторное использование порта (полезно при частых перезапусках)
    int reuse_address = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse_address, sizeof(reuse_address));

    // Инициализируем адресную структуру
    sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);  // Слушать все интерфейсы
    address.sin_port = htons(static_cast<unsigned short>(port));  // Конвертируем порт в сетевой порядок байт

    // Привязываем сокет к адресу и порту
    if (bind(listen_fd_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0)
    {
        perror("MJPEG bind");
        stop();
        return false;
    }

    // Переводим сокет в режим прослушивания (очередь подключений = 1)
    if (listen(listen_fd_, 1) < 0)
    {
        perror("MJPEG listen");
        stop();
        return false;
    }

    // Включаем неблокирующий режим: accept/send не должны блокировать основной цикл (например, инференс YOLO)
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

    // Пытаемся принять новое подключение (неблокирующе из-за O_NONBLOCK)
    int new_client = accept(listen_fd_, reinterpret_cast<sockaddr *>(&client_address), &address_size);
    if (new_client < 0)
    {
        // EAGAIN/EWOULDBLOCK — нет подключения прямо сейчас; EINTR — прервано сигналом
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
        {
            perror("MJPEG accept");
        }
        return;
    }

    // Поддерживается только один клиент: при новом подключении старое соединение закрывается
    closeClient();
    client_fd_ = new_client;

    // Устанавливаем таймаут отправки: если клиент не читает, сервер не должен «висеть» на send
    timeval send_timeout;
    send_timeout.tv_sec = 0;
    send_timeout.tv_usec = 250000;  // 250 мс
    setsockopt(client_fd_, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout));

    // Отправляем HTTP-заголовки потока: клиент понимает, что это MJPEG
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

    // Гарантируем отправку всех байтов: send может вернуть меньше, чем запрошено
    while (sent < size)
    {
        ssize_t result = send(client_fd_, bytes + sent, size - sent, MSG_NOSIGNAL);
        if (result < 0)
        {
            // EINTR — системный вызов прерван сигналом: пробуем снова
            if (errno == EINTR)
            {
                continue;
            }
            return false;  // Ошибка отправки
        }
        if (result == 0)
        {
            return false;  // Клиент закрыл соединение
        }
        sent += static_cast<size_t>(result);
    }
    return true;  // Все данные отправлены
}

void MjpegServer::publish(const cv::Mat &frame)
{
    // Если сервер не запущен — ничего не делаем
    if (listen_fd_ < 0)
    {
        return;
    }

    // Пытаемся принять клиента (если его ещё нет)
    acceptClient();

    // Если нет клиента или кадр пустой — выходим
    if (client_fd_ < 0 || frame.empty())
    {
        return;
    }

    // Кодируем кадр в JPEG с качеством 75%
    std::vector<unsigned char> jpeg;
    std::vector<int> jpeg_params;
    jpeg_params.push_back(cv::IMWRITE_JPEG_QUALITY);
    jpeg_params.push_back(75);
    if (!cv::imencode(".jpg", frame, jpeg, jpeg_params))
    {
        fprintf(stderr, "Failed to encode frame as JPEG\n");
        return;
    }

    // Формируем заголовок части (boundary + Content-Type + Content-Length)
    char part_header[128];
    int header_size = snprintf(part_header, sizeof(part_header),
                               "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n",
                               jpeg.size());

    // Проверяем корректность заголовка и отправляем:
    // 1) заголовок части, 2) JPEG-данные, 3) разделитель \r\n между кадрами
    if (header_size <= 0 || static_cast<size_t>(header_size) >= sizeof(part_header) ||
        !sendAll(part_header, static_cast<size_t>(header_size)) ||
        !sendAll(jpeg.data(), jpeg.size()) ||
        !sendAll("\r\n", 2))
    {
        printf("MJPEG client disconnected or too slow\n");
        closeClient();  // Клиент не успевает читать — закрываем соединение
    }
}

void MjpegServer::closeClient()
{
    if (client_fd_ >= 0)
    {
        shutdown(client_fd_, SHUT_RDWR);  // Сообщаем о завершении передачи
        close(client_fd_);                // Закрываем дескриптор
        client_fd_ = -1;                  // Сбрасываем в «неактивное» состояние
    }
}

void MjpegServer::stop()
{
    closeClient();  // Сначала закрываем клиента
    if (listen_fd_ >= 0)
    {
        close(listen_fd_);  // Затем слушающий сокет
        listen_fd_ = -1;    // Сбрасываем флаг
    }
}
