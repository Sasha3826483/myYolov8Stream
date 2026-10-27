#include <stdint.h> // Типы фиксированной разрядности, например uint8_t.
#include <stdio.h> // Вывод сообщений и результатов в консоль.
#include <stdlib.h> // Общие функции C и управление памятью.
#include <string.h> // Работа с C-строками и памятью.
#include <algorithm> // std::max для ограничения координаты подписи.
#include <string> // Строка подписи для OpenCV.
#include "yolov8.h" // Интерфейс проекта для RKNN, модели и постобработки YOLO.
#include <opencv2/core/core.hpp> // Базовые типы OpenCV: Mat, Point, Scalar.
#include <opencv2/highgui/highgui.hpp> // Высокоуровневый ввод-вывод OpenCV, включая захват видео.
#include <opencv2/imgproc/imgproc.hpp> // Обработка изображений: resize, rectangle, putText.
#include <chrono> // Замер времени чтения кадра.
#include "mjpeg_server.h" // HTTP-сервер для передачи обработанных кадров в формате MJPEG.

#define MODEL_INPUT_SIZE 640
#define CAMERA_DEV 11

// Цвет рамки для внутренних ID классов: 0 — person, 1 — bicycle.
cv::Scalar classColorMap[2] = {cv::Scalar(255, 255, 0),
                               cv::Scalar(0, 255, 0)};

int main(int argc, char **argv)
{
    // Программа принимает ровно один аргумент — путь к RKNN-модели.
    if (argc != 2)
    {
        printf("%s <model_path>\n", argv[0]);
        return -1;
    }

    const char *model_path = argv[1];

    int ret;
    rknn_app_context_t rknn_app_ctx;
    // Обнуляем контекст перед тем, как в него запишутся RKNN-ресурсы.
    memset(&rknn_app_ctx, 0, sizeof(rknn_app_context_t));

    // Загружаем имена классов, которые используются при печати и подписи рамок.
    init_post_process();

    // Создаём контекст NPU и выделяем входные/выходные буферы для модели.
    ret = init_yolov8_model(model_path, &rknn_app_ctx);
    if (ret != 0)
    {
        printf("init_yolov8_model fail! ret=%d model_path=%s\n", ret, model_path);
    }
    // OpenCV-матрица оборачивает память входного тензора RKNN без отдельного буфера.
    cv::Mat bgr640(MODEL_INPUT_SIZE, MODEL_INPUT_SIZE, CV_8UC3, rknn_app_ctx.input_mems[0]->virt_addr);

    cv::VideoCapture cap;
    // Запрашиваем у камеры размер кадра модели и открываем камеру с индексом CAMERA_DEV.
    cap.set(cv::CAP_PROP_FRAME_WIDTH, MODEL_INPUT_SIZE);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, MODEL_INPUT_SIZE);
    cap.open(CAMERA_DEV);
    cv::Mat camFrame;

    // Запускаем MJPEG-сервер на всех сетевых интерфейсах, включая USB Ethernet.
    MjpegServer stream_server;
    if (!stream_server.start(8080))
    {
        fprintf(stderr, "Failed to start MJPEG stream server\n");
        return -1;
    }

    // Непрерывная обработка кадров: захват → масштабирование → инференс → детекции.
    while (1)
    {
        // Замеряем только время чтения кадра с камеры, не полный цикл инференса.
        std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
        cap >> camFrame;
        std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();

        printf("Frame latency: %lf\n", std::chrono::duration<double>(end - begin).count());

        // Масштабируем кадр до размера входа модели; результат записывается в буфер RKNN.
        cv::resize(camFrame, bgr640, cv::Size(MODEL_INPUT_SIZE, MODEL_INPUT_SIZE), 0, 0, cv::INTER_LINEAR);

        // Запускаем нейросеть на NPU; выходные тензоры сохраняются в контексте модели.
        rknn_run(rknn_app_ctx.rknn_ctx, nullptr);

        // Преобразуем выходы YOLO в список рамок и классов.
        // 0.25 — минимальная уверенность, 0.45 — порог подавления перекрывающихся рамок (NMS).
        object_detect_result_list od_results;
        post_process(&rknn_app_ctx, rknn_app_ctx.output_mems, 0.25, 0.45, &od_results);

        // Для каждой детекции печатаем класс и уверенность, затем рисуем рамку и подпись.
        for (int i = 0; i < od_results.count; i++)
        {
            object_detect_result *det_result = &(od_results.results[i]);

            // Координаты прямоугольника в системе координат входа модели.
            int x1 = det_result->box.left;
            int y1 = det_result->box.top;
            int x2 = det_result->box.right;
            int y2 = det_result->box.bottom;
            cv::Rect r = cv::Rect(x1, y1, x2 - x1, y2 - y1);

            // Выводим название класса и confidence в консоль.
            printf("%s: %.2f\n", coco_cls_to_name(det_result->cls_id), det_result->prop);

            // Рисуем рамку на исходном кадре.
            cv::rectangle(camFrame, r, classColorMap[det_result->cls_id], 1, 8, 0);

            // Формируем текст для изображения: имя класса и уверенность.
            std::string label = cv::format(
                "%s %.2f",
                coco_cls_to_name(det_result->cls_id),
                det_result->prop
            );

            // Рисуем подпись около верхнего края рамки; y не даём уйти за край кадра.
            cv::putText(
                camFrame,
                label,
                cv::Point(x1, std::max(y1 - 5, 15)),
                cv::FONT_HERSHEY_SIMPLEX,
                0.5,
                classColorMap[det_result->cls_id],
                1,
                cv::LINE_AA
            );
        }

        // Отправляем обработанный кадр подключённому браузеру по USB-сети.
        stream_server.publish(camFrame);
    }

    return 0;
}
