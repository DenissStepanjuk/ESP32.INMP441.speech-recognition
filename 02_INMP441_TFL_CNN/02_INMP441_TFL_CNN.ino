/**
  Подготовил: Степанюк Денис Борисович
**/

// TensorFlowLite_ESP32-----------------------------------------------------
// Модель машиного обучения.
#include "TensorFlowLiteModel.h"
// Параметры изображения передаваемого модели, а так же кол-во категорий для классификации.
#include "TensorFlowLiteModelConfig.h"
// -------------------------------------------------------------------------


/** В документе реализлваны функции для обработки аудио и построения спектрограмы. **/
#include <Audio_processing.h>

// Пины для подключения светодиодов.
const int LED1 = 19;  
const int LED2 = 20;
const int LED3 = 21;


// ===============================
// Настройка
// ===============================
void setup() {
  // Прописываем WAV-заголовок в буфере wav_buffer.
  create_wav_header(wav_buffer, DATA_SIZE);

  // TensorFlowLite_ESP32---------------------------------------------------------------------------------------------------------
  // Для ведения журнала ошибок создадим переменную "error_reporter" на базе предоставляемых библиотекой Tensor Flow Lite структур данных.
  //static tflite::MicroErrorReporter micro_error_reporter;
  static tflite::MicroErrorReporter micro_error_reporter;
  // Переменную "error_reporter" необходимо передать в интерпретатор, который будет в свою очередь передавать в неё список ошибок.
  error_reporter = &micro_error_reporter;


  // Создадим экземпляр модели используя массив данных из документа "TensorFlowLiteModel.h" 
  model = tflite::GetModel(model_TFLite);
  // Проверка соответствия версии модели и версии библиотеки.
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    TF_LITE_REPORT_ERROR(error_reporter,
                         "Model provided is schema version %d not equal "
                         "to supported version %d.",
                         model->version(), TFLITE_SCHEMA_VERSION);
    return;
  }
  // Выделить обьём памяти для входного, выходного и промежуточных массивов модели,
  if (tensor_arena == NULL) {
    // Выделить более медляную память, но большую по обьёму.
    //tensor_arena = (uint8_t*) ps_calloc(kTensorArenaSize, 1);
    // Выделить более быструю память, но меньшую по обьёму.
    //tensor_arena = (uint8_t *) heap_caps_malloc(kTensorArenaSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    tensor_arena = (uint8_t*) malloc(kTensorArenaSize);
  }
  // Если не удалось выделить обьём памяти для входного, выходного и промежуточных массивов модели, то вывести сообщение об ошибке.
  if (tensor_arena == NULL) {
    printf("Couldn't allocate memory of %d bytes\n", kTensorArenaSize);
    return;
  }


  // Загрузить все методы, что содержит библиотека Tensor Flow Lite, для обработки данных моделью. (Занимает большой обьём памяти)
  // tflite::AllOpsResolver resolver;

  // Загрузить необходимые методы для обработки данных моделью из библиотеки Tensor Flow Lite.
  static tflite::MicroMutableOpResolver<9> micro_op_resolver;
  // AveragePool2D — операция, применяемая в свёрточных нейронных сетях (CNN), для уменьшения ширины и высоты входного тензора.
  micro_op_resolver.AddAveragePool2D();
  // MaxPool2D — операция в свёрточных нейронных сетях (CNN), которая выполняет подвыборку данных, уменьшая ширину и высоту входного тензора.
  micro_op_resolver.AddMaxPool2D();
  // Reshape — операция, используемая в машинном обучении и обработке данных, которая изменяет форму (размерность) тензора без изменения его данных
  micro_op_resolver.AddReshape();
  // FullyConnected (полносвязанный слой) — используется для выполнения нелинейных преобразований данных и играет важную роль в моделях глубокого обучения.
  micro_op_resolver.AddFullyConnected();
  // Conv2D (свёрточный слой) — выполняет операцию свёртки над входными данными, чтобы извлекать локальные признаки, использует их для построения более сложных представлений на следующих слоях.
  micro_op_resolver.AddConv2D();
  // DepthwiseConv2D — разновидность свёрточного слоя, которая применяется для увеличения вычислительной эффективности и уменьшения количества параметров модели.
  micro_op_resolver.AddDepthwiseConv2D();
  // Softmax — функция активации, которая используется в выходных слоях нейронных сетей для задач классификации.
  micro_op_resolver.AddSoftmax();
  // Quantize (квантование) — процесс преобразования данных или моделей глубокого обучения, чтобы снизить их размер и вычислительную сложность, сохраняя при этом приемлемую точность.
  micro_op_resolver.AddQuantize();
  // Dequantize (деквантование) — процесс обратного преобразования данных из квантованного формата обратно в формат с плавающей точкой или в более высокую точность. 
  micro_op_resolver.AddDequantize();


  // Создадим экземпляр интерпретатора передавав необходимые данные для запуска модели.
  static tflite::MicroInterpreter static_interpreter(
    model, micro_op_resolver, tensor_arena, kTensorArenaSize);
      //model, micro_op_resolver, tensor_arena, kTensorArenaSize, error_reporter);

  interpreter = &static_interpreter;


  // Выделим память для внутрених тензоров модели из выделеной ранее памяти tensor_arena.
  TfLiteStatus allocate_status = interpreter->AllocateTensors();
  // При неудачном выделении памяти сообщить об ошибке.
  if (allocate_status != kTfLiteOk) {
    TF_LITE_REPORT_ERROR(error_reporter, "AllocateTensors() failed");
    return;
  }

  // Получить указатель на входной тензор модели.
  input = interpreter->input(0);
  // TensorFlowLite_ESP32---------------------------------------------------------------------------------------------------------


  // Инициализирует последовательный порт (UART) для связи ESP32 ↔ компьютер.
  Serial.begin(115200);
  Serial.println("I2S microphone record demo");
  // Инициализация I2S
  i2s_install();
  // Настройка пинов I2S
  i2s_setpin();
  // Запускает периферию I2S в режиме приёма/передачи данных.
  i2s_start(I2S_PORT);
  // Задержка 500 миллисекунд.
  delay(500);


  // Инициализация пинов ESP32 для светодиодов.
  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(LED3, OUTPUT);

  // Выключаем все LED.
  digitalWrite(LED1, LOW);
  digitalWrite(LED2, LOW);
  digitalWrite(LED3, LOW);

}


void loop() {
  // Записываем 1 секунду аудио в буфере wav_buffer.
  record_to_buffer();
  
  //  - uint8_t * d_buff: Указатель на исходное тихое аудио.
  uint8_t* src  = wav_buffer + WAV_HEADER_SIZE;    // исходные данные (ADC-формат 12 бит)
  //  - uint8_t* s_buff: Указатель для запизи громкого аудио.
  uint8_t* dest = wav_buffer + WAV_HEADER_SIZE;    // будем конвертировать "на месте"
  // Функция увеличивает громкость аудио.
  audio_scale(dest, src, DATA_SIZE);
  
  // Указатель на аудиосигнал преобразуемый в спектрограму.
  int16_t *pcm16 = (int16_t*)(wav_buffer + WAV_HEADER_SIZE);
  // Двумерный массив для спектрограммы (каждая строка = pooled frequency bins).
  float **spec;
  // Сюда функция get_spectrogram() запишет количество временных кадров спектрограммы.
  int frames;
  // Построить спектрограмму.
  bool above_noise = get_spectrogram(pcm16, SAMPLES_COUNT, spec, frames);


  // TensorFlowLite_ESP32---------------------------------------------------------------------------------------------------------
  // Если громкость аудио записи выше порогового значения, то выполнить инференс модели.
  if(smoothed_noise_floor > 2.2){
    Serial.println("------------- // smoothed_noise_floor > 4.5 // -------------");
    // Входной тензор модели (вход модели).
    float * input_data = input->data.f;

    // Индекс текущего элемента входного тензора модели.
    int input_idx = 0;
    // Передаём спектрограмму (в порядке NHWC: [frames][bins][1]) на вход свёрточной нейронной сети.
    for (int f = 0; f < frames; f++) {
        for (int b = 0; b < POOLED_BINS; b++) {
            input_data[input_idx] = spec[f][b];
            input_idx++;
        }
    }
    
    // Вызвать модель (произвести преобразование входного изображения в вероятность принадлежности 
    // данного изображения к каждому из возможных классов).
    if (kTfLiteOk != interpreter->Invoke()) {
      TF_LITE_REPORT_ERROR(error_reporter, "Invoke failed.");
    }

    // Получить выход модели.
    TfLiteTensor* output = interpreter->output(0);

    // Массив для хранения вероятностей для всех классов.
    int8_t probabilities [kCategoryCount];
    // Пройти по каждому элементу выхода модели.
    for(int i = 0; i < kCategoryCount; i++){
      // Получить вероятность для i-го класса.
      probabilities [i] = output->data.uint8[i];
    }

    // Получим наименование предсказаной категории.
    String prediction = getPrediction(kCategoryCount, probabilities, kCategoryLabels);

    Serial.printf("Prediction: %s\n", prediction);

    // Включить предсказаное кол-во светодиодов.
    setLedsByPrediction(prediction);
  
  }

  // Обнулить оценку уровня шума. Используется для детекции речи/голоса (VAD).
  smoothed_noise_floor = 0.0f;

  // Освободить выделенную память под спектрограмму.
  free_spectrogram(spec, frames);
  
}





// -----------------------------
// Функция включения LED по предсказанию
// -----------------------------
void setLedsByPrediction(String pred) {
  int leds_to_turn_on = 0;

  // Интерпретируем строку предсказания
  if (pred == "0_Zero")  leds_to_turn_on = 0;
  else if (pred == "1_One") leds_to_turn_on = 1;
  else if (pred == "2_Two") leds_to_turn_on = 2;
  else if (pred == "3_Three") leds_to_turn_on = 3;
  else {
    Serial.println("Unknown prediction");
    return;
  }

  Serial.print("Prediction: ");
  Serial.print(pred);
  Serial.print(" -> LEDs ON: ");
  Serial.println(leds_to_turn_on);

  // Управление светодиодами
  switch (leds_to_turn_on) {
    case 0:
      digitalWrite(LED1, LOW);
      digitalWrite(LED2, LOW);
      digitalWrite(LED3, LOW);
      break;

    case 1:
      digitalWrite(LED1, HIGH);
      digitalWrite(LED2, LOW);
      digitalWrite(LED3, LOW);
      break;

    case 2:
      digitalWrite(LED1, HIGH);
      digitalWrite(LED2, HIGH);
      digitalWrite(LED3, LOW);
      break;

    case 3:
      digitalWrite(LED1, HIGH);
      digitalWrite(LED2, HIGH);
      digitalWrite(LED3, HIGH);
      break;

    default:
      // На всякий случай выключаем всё
      digitalWrite(LED1, LOW);
      digitalWrite(LED2, LOW);
      digitalWrite(LED3, LOW);
      break;
  }
}

