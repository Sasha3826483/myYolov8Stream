// Copyright (c) 2023 by Rockchip Electronics Co., Ltd. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#ifndef _RKNN_DEMO_YOLOV8_H_
#define _RKNN_DEMO_YOLOV8_H_

#include "rknn_api.h"
#include "common.h"

#if defined(RV1106_1103) 
    // Описание DMA-буфера изображения для платформ RV1106/RV1103.
    typedef struct {
        char *dma_buf_virt_addr; // Адрес буфера, доступный процессу CPU.
        int dma_buf_fd;          // Дескриптор буфера для передачи драйверу/NPU.
        int size;                // Размер буфера в байтах.
    }rknn_dma_buf;
#endif

// Контекст приложения для работы с RKNN-моделью YOLOv8.
// Инициализируется init_yolov8_model() и затем передаётся функциям инференса
// и постобработки, чтобы они могли использовать общие ресурсы модели.
typedef struct {
    rknn_context rknn_ctx;                 // Основной дескриптор RKNN-модели/NPU.
    rknn_input_output_num io_num;          // Количество входных и выходных тензоров.
    rknn_tensor_attr* input_attrs;         // Метаданные входных тензоров модели.
    rknn_tensor_attr* output_attrs;        // Метаданные выходных тензоров модели.
#if defined(RV1106_1103) 
    rknn_tensor_mem* input_mems[1];        // Выделенная память входного тензора.
    rknn_tensor_mem* output_mems[9];       // Выделенная память выходных тензоров.
    rknn_dma_buf img_dma_buf;              // DMA-буфер для изображения на этой платформе.
#endif
    int model_channel;                     // Число каналов входного изображения.
    int model_width;                       // Ширина входа модели в пикселях.
    int model_height;                      // Высота входа модели в пикселях.
    bool is_quant;                         // true, если выходы модели квантизованы.
} rknn_app_context_t;

#include "postprocess.h"


// Загружает модель из файла и подготавливает контекст, атрибуты и буферы RKNN.
// Возвращает код результата; app_ctx заполняется при успешной инициализации.
int init_yolov8_model(const char* model_path, rknn_app_context_t* app_ctx);

// Освобождает созданные для модели ресурсы и сбрасывает связанные данные контекста.
int release_yolov8_model(rknn_app_context_t* app_ctx);

// Выполняет полный цикл инференса для изображения и записывает найденные объекты
// в od_results. Возвращает код результата выполнения.
int inference_yolov8_model(rknn_app_context_t* app_ctx, image_buffer_t *img,  object_detect_result_list* od_results);

#endif //_RKNN_DEMO_YOLOV8_H_
