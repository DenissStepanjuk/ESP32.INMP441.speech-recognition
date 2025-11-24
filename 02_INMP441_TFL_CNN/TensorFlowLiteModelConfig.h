#ifndef TENSORFLOW_LITE_CONFIG
#define TENSORFLOW_LITE_CONFIG

// TensorFlowLite_ESP32-----------------------------------------------------
// Реализация ряда функций обработки данных моделью необходимых интерпретатору для запуска модели.
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_op_resolver.h"
// Ряд функций предоставляющих отчёт об ошибках и отладочную информацию.
#include "tensorflow/lite/micro/tflite_bridge/micro_error_reporter.h"
//#include "tensorflow/lite/micro/micro_error_reporter.h"
// Интерпретатор содержит код для загрузки и запуска модели без предвапительной компиляции.
// (выполняет программу построчно, читая каждую инструкцию и преобразуя её в исполняемый код)
#include "tensorflow/lite/micro/micro_interpreter.h"
// Cодержит схему преобразования модели в поток байтов для передачи в память на базе библиотеки FlatBuffers.
// (FlatBuffers — эффективная кроссплатформенная библиотека сериализации для C++, C#, C)
#include "tensorflow/lite/schema/schema_generated.h"
// Этот файл определяет общие типы данных и API для реализации операций, делегатов и других конструкций в TensorFlow Lite.
#include "tensorflow/lite/c/common.h"


// Кол-во строк (векторов) передаваемых на вход модели.(41x99 = 4 059)
constexpr int height = 99;
// Кол-во элементов в одной строке (векторе) передаваемой на вход модели.
constexpr int wide = 41;

// Кол-во параметров передаваемых на вход модели.
constexpr int inputVectoSize = 4059;

// Кол-во классов предсказываемых моделью.
//constexpr int kCategoryCount = 10;
constexpr int kCategoryCount = 4;

// Наименования категорий, которые модель может классифицировать.
//const char* kCategoryLabels[kCategoryCount] = {"0_Zero", "1_One", "2_Two", "3_Three", "4_Four", "5_Five", "6_Six", "7_Seven", "8_Eight", "9_Nine"};
const char* kCategoryLabels[kCategoryCount] = {"0_Zero", "1_One", "2_Two", "3_Three"};

// Инициализировать необходимые структуры данных для работы с библиотекой Tensor Flow Lite.
// Обьект ErrorReporter предоставляет отчёт об ошибках и отладочную информацию.
tflite::ErrorReporter* error_reporter = nullptr;
// Обьект для хранения модели машиного обучения осуществляющей классификацию изображения.
const tflite::Model* model = nullptr;
// Обьект для хранения интерпретатора осуществляющего загрузку и запуск модели.
tflite::MicroInterpreter* interpreter = nullptr;
// Обьект (тензор) для хранения изображения передаваемого на вход модели для последующей классификации.
TfLiteTensor* input = nullptr;

// Обьём памяти, который необходимо выделить для хранения массивов модели.
// Для входного, выходного и промежуточных массивов модели.
//constexpr int kTensorArenaSize = 81 * 1024;
constexpr int kTensorArenaSize = 100 * 1024;
// Массив для хранения входных, выходных и промежуточных массивов модели.
static uint8_t *tensor_arena;//[kTensorArenaSize]; // Maybe we should move this to external



/** Функция возвращает наименование категории захваченой датчика.
  int kCategoryCount - Кол-во классов предсказываемых моделью.
  int8_t probabilities [] - Массив для хранения вероятностей для всех классов.
  char* kCategoryLabels[] - Массив наименований категорий, которые модель может классифицировать.   **/
String getPrediction(int kCategoryCount, int8_t probabilities [], const char* kCategoryLabels[]){
  // Индекс для категории с наибольшей вероятностью.
  int idx = 0;
  // Максимальная вероятность.
  int8_t maxProbability = probabilities[idx];

  // Пройдём через все сущности "категории" для которых модель возвращает вероятность.
  for (int i = 1; i < kCategoryCount; i++) {
    // Если вероятность для текущей сущности "категории" больше максимальная вероятности назначеной ранее,
    if(probabilities[i] > maxProbability){
      // Обновим максимальную вероятность.
      maxProbability = probabilities[i];
      // Обновим индекс для категории с наибольшей вероятностью.
      idx = i;
    }
  }
  // Вернём наименование категории с наибольшей вероятностью.
  return String(kCategoryLabels[idx]);
}

#endif  // TENSORFLOW_LITE_CONFIG