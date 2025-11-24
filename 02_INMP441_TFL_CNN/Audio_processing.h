// ===============================
// KISS FFT - Библиотека для быстрого преобразование Фурье.
// https://github.com/mborgerding/kissfft
// ===============================
#include <kissfft/kiss_fft.h>
#include <kissfft/kiss_fft.c>
#include <kissfft/kiss_fftr.h>
#include <kissfft/kiss_fftr.c>
#include <kissfft/_kiss_fft_guts.h>

/** В документе реализлваны функции для подключения микрофона и записи аудио. **/
#include <Audio_recording.h>

// Configuration
#define FFT_N 320             // Размер окна для дискретного преобразования Фурье DFT/FFT (320 точек).
#define FFT_STEP 160          // Шаг (hop size) между соседними окнами. Окна перекрывают друг друга на 50%.
#define POOLING_SIZE 4        // Количество частотных бинов, которые усредняються (pool) вместе, чтобы уменьшить размер спектрограммы.
#define SPECTRUM_BINS (FFT_N/2 + 1)  // Число уникальных частотных бинов FFT для окна длины FFT_N (161 бин до усреднения/pooling).
#define POOLED_BINS ((SPECTRUM_BINS + POOLING_SIZE - 1) / POOLING_SIZE)  // Число частотных бинов FFT для окна длины FFT_N после усреднения/pooling (~41 bins).
#define EPSILON 1e-6f         // Маленькая константа для числовой стабильности (избежать деления на ноль и лог(0)) при последующей обработке.
#define AUDIO_LENGTH 16000    // Длина аудио-сигнала в сэмплах: 16000 — одна секунда при частоте 16 kHz.

// Global FFT configuration
kiss_fftr_cfg fft_cfg = NULL;         // Указатель для конфигурации библиотечного real-FFT.
kiss_fft_cpx fft_out[SPECTRUM_BINS];  // Статический массив структур kiss_fft_cpx.
float energy[SPECTRUM_BINS];          // Массив для хранения энергетических величин (модуль спектра, magnitude или magnitude squared) для каждого бина. Используется для логарифмирования, нормализации и построения спектрограммы.
float smoothed_noise_floor = 0.0f;    // Скаляр, хранящий оценку уровня шума (напр., средний фон по времени), сглаженную экспоненциально, обновляется при обработке кадров. Часто используется для детекции речи/голоса (VAD).


// Hamming window coefficients (pre-computed)
float hamming_coeffs[FFT_N]; // Массив коэффициентов окна длиной FFT_N. Заранее рассчитывается в init_hamming_window() чтобы не тратить CPU каждый кадр.

// ===============================
// Инициализировать коэффициенты окна Хэмминга.
// ===============================
void init_hamming_window() {
  // Вычисляем константу arg = 2π / N. Она будет умножаться на индекс, чтобы получить аргумент для cos.
  const float arg = 2.0f * PI / FFT_N;
  // Цикл по всем индексам окна от 0 до N-1:
  for (int i = 0; i < FFT_N; i++) {
    // Вычисляется значение окна Хэмминга для позиции i.
    hamming_coeffs[i] = 0.5f - 0.5f * cosf(arg * (i + 0.5f));
  }
}

// ===============================
// Применить окно Хэмминга.
// float *buffer: Входной сэмпл для дискретного преобразования Фурье DFT/FFT (320 точек).
// int n: Размер входного сэмпла (320 точек).
// ===============================
void apply_hamming(float *buffer, int n) {
  // Проходим по всем точкам входного сэмпла (320 точек) и каждую точку умножаем на соответсвующий коэфицент Хэмминга.
  for (int i = 0; i < n; i++) {
    buffer[i] *= hamming_coeffs[i];
  }
}


// ===============================
// Get spectrogram segment (one window) with pooling
//  - float *fft_in: Входной сэмпл для дискретного преобразования Фурье DFT/FFT (320 точек).
//  - float *output: Рассчитанная для входного сэмпла амплитудно-частотная характеристика.
// ===============================
void get_spectrogram_segment(float *fft_in, float *output) {
  // Применить окно Хэмминга.
  apply_hamming(fft_in, FFT_N);
  // ✅ Проверка что fft_cfg инициализирован
  if (!fft_cfg) {
    Serial.println("ERROR: FFT config not initialized!");
    return;
  }
 
  // Применить дискретное преобразование Фурье DFT/FFT.
  // - fft_cfg — предварительно выделённая конфигурация (через kiss_fftr_alloc).
  // - fft_in — массив FFT_N float (временная область).
  // - fft_out — массив kiss_fft_cpx длиной SPECTRUM_BINS (выхoд: комплексные частотные бины).
  kiss_fftr(fft_cfg, fft_in, fft_out);
  // Вычислить квадрат модуля комплексного числа (энергии) → мощность на всех бинах.
  for (int i = 0; i < SPECTRUM_BINS; i++) {
    // реальные части i-го бина
    float re = fft_out[i].r;
    // мнимые части i-го бина
    float im = fft_out[i].i;
    // Вычисление энергии (power) бина. re^2 + im^2 — это квадрат модуля комплексного числа → мощность на данном бине (magnitude-squared).
    energy[i] = re * re + im * im;
  }
  // Применить усреднение (уменьшить частотную размерность для получения компактного представления):
  // Индекс для массива усреднёных значений.
  int output_idx = 0;
  // Каждый шаг усредняет группу бинов.
  for (int i = 0; i < SPECTRUM_BINS; i += POOLING_SIZE) {
    // Усреднёнон значение.
    float average = 0.0f;
    // Счётчика бинов.
    int count = 0;
    // Складываем все элементы в группуе бинов.
    for (int j = 0; j < POOLING_SIZE && (i + j) < SPECTRUM_BINS; j++) {
      average += energy[i + j];
      count++;
    }
    // Получим усреднённое значение для группы бинов.
    average /= count;
    // Логарифмическое преобразование.
    output[output_idx] = log10f(average + EPSILON);
    output_idx++;
  }
}


// ===============================
// Основная функция для построения спектрограммы с определением уровня шума.
// Возвращает true, если уровень звука превышает уровень шума.
//  - const int16_t *pcm: входной буфер PCM-сэмплов (Pulse Code Modulation - Импульсно-кодовая модуляция (ИКМ)).
//  - size_t sample_count: длина входного буфера в сэмплах
//  - float **&spec_out: двумерный динамический массив для хранения спектрограммы.
//  - int &frames_out: выходной параметр, в который функция записывает число временных кадров (строк) в spec_out.
// ===============================
bool get_spectrogram(const int16_t *pcm, size_t sample_count, float **&spec_out, int &frames_out){
  /// Инициализируем Быстрое Преобразование Фурье, если это ещё не было сделано.
  if (!fft_cfg) {
    //Serial.println("Initializing FFT...");
    //Serial.println("FFT_N = " + String(FFT_N));
    // kiss_fftr_alloc возвращает конфигурацию, которую нужно сохранить и переиспользовать.
    fft_cfg = kiss_fftr_alloc(FFT_N, 0, NULL, NULL);
    if (!fft_cfg) {
      Serial.println("ERROR: kiss_fftr_alloc() FAILED! Returned NULL!");
      Serial.println("Check: FFT_N must be > 0");
      Serial.println("Check: Available RAM");
      return false;
    }
    // Инициализируем коэффициенты окна (Hamming/Hann), предварительно (однократно).
    init_hamming_window();
  }
  // Рассчитать количество кадров.
  frames_out = 1 + (sample_count - FFT_N) / FFT_STEP;
  
  // Выделить двухмерный массив для спектрограммы.
  // spec_out будет указывать на массив указателей на строки (каждая строка = pooled frequency bins)
  spec_out = new float*[frames_out];
  for (int i = 0; i < frames_out; i++) {
    // для каждой строки выделяем массив float длины POOLED_BINS
    spec_out[i] = new float[POOLED_BINS];
  }
  // Рассчитать среднее значение.
  float mean = 0.0f;
  for (size_t i = 0; i < sample_count; i++) {
    // суммируем все сэмплы (int16 -> неявно приводится к float)
    mean += pcm[i];
  }
  // делим на количество сэмплов -> получаем среднее значение выборки (DC offset)
  mean /= sample_count;
  
  // Рассчитать максимальное абсолютное значение и уровень шума
  float max_val = 0.0f;
  float noise_floor = 0.0f;
  int samples_over_noise_floor = 0;
  // Пройти по всем сэмплам.
  for (size_t i = 0; i < sample_count; i++) {
    // абсолютное отклонение от среднего.
    float value = fabsf((float)pcm[i] - mean);
    // Максимальное абсолютное отклонение; это используется для нормировки (делим на max_val).
    max_val = max(max_val, value);
    // Накапливаем сумму абсолютных отклонений.
    noise_floor += value;
    
    // Cчитаем сэмплы, которые в текущем файле более чем в 5 раз выше текущего сглаженного шума. Это простой детектор активности.
    if (value > 5.0f * smoothed_noise_floor) {
      samples_over_noise_floor++;
    }
  }
  // Среднее абсолютное отклонение.
  noise_floor /= sample_count;
  
  // Обновить сглаженный уровень шума.
  if (noise_floor < smoothed_noise_floor) {
    // если новый оценочный уровень ниже имеющегося — более быстрый спад
    smoothed_noise_floor = 0.7f * smoothed_noise_floor + 0.3f * noise_floor;
  } else {
    // если шум подрос — обновляем медленно, чтобы избежать всплесков
    smoothed_noise_floor = 0.99f * smoothed_noise_floor + 0.01f * noise_floor;
  }
  
  // Избегаем деления на ноль, если сигнал почти нулевой, используем 1.0 чтобы не делить на ноль.
  if (max_val < EPSILON) {
    max_val = 1.0f;
  }
  // Обрабатываем каждое окно.
  // временный буфер для FFT входа; размер FFT_N (float)
  float fft_in[FFT_N];
  int frame_idx = 0;
  
  // Проходим по всем сэмплам на которые был разбит аудио сигнал.
  for (size_t start = 0; start + FFT_N <= sample_count; start += FFT_STEP) {
    // Нормализуем выборки: вычитаем среднее и делим на максимум
    for (int i = 0; i < FFT_N; i++) {
      fft_in[i] = ((float)pcm[start + i] - mean) / max_val;
    }
    
    // Вычислить сегмент спектрограммы (АЧХ для аудиосэмпла).
    get_spectrogram_segment(fft_in, spec_out[frame_idx]);
    
    // Увеличить индекс кадров.
    frame_idx++;
    // Безопасный выход если достигли предела
    if (frame_idx >= frames_out) break;
  }
  
  // Проверка, достаточно ли звука выше уровня шума (>5% от выборок)
  bool above_noise = samples_over_noise_floor > (sample_count / 20);
  
  // Возвращаем true если есть активность выше шума
  return above_noise;
}





// ===============================
// Освободить выделенную память под спектрограмму.
//  - float **spec: двухмерный массив для спектрограммы (каждая строка = pooled frequency bins).
//  - int frames: число временных кадров (строк) в спектрограмме.
// ===============================
void free_spectrogram(float **spec, int frames) {
  for (int i = 0; i < frames; i++) {
    delete[] spec[i];
  }
  delete[] spec;
}




// ===============================
// Example usage function
//  - const int16_t *pcm16: Указатель на аудиосигнал преобразуемый в спектрограму.
//  - size_t sample_count: Размер дискретизоного аудиосигнала (16000 точек).
// ===============================
void process_audio_to_spectrogram(const int16_t *pcm16, size_t sample_count)
{
  // Двумерный массив для спектрограммы (каждая строка = pooled frequency bins).
  float **spec;
  // Сюда функция get_spectrogram() запишет количество временных кадров спектрограммы.
  int frames;
  // Построить спектрограмму.
  bool above_noise = get_spectrogram(pcm16, sample_count, spec, frames);

  // Выводим в UART: размеры спектрограммы, есть ли звук, текущий сглаженный уровень шума.
  //Serial.printf("Spectrogram: %d frames x %d bins (pooled)\n", frames, POOLED_BINS);
  //Serial.printf("Above noise floor: %s\n", above_noise ? "YES" : "NO");
  //Serial.printf("Smoothed noise floor: %.6f\n", smoothed_noise_floor);



  // TensorFlowLite_ESP32---------------------------------------------------------------------------------------------------------
  if(smoothed_noise_floor > 2.2){
    Serial.println("------------- // smoothed_noise_floor > 4.5 // -------------");
    // Входной тензор модели (вход модели).
    float * input_data = input->data.f;
    //Serial.printf("Model input type = %d\n", input->type);
    //Serial.printf("Input dims: %d x %d x %d x %d\n",
    //          input->dims->data[0],
    //          input->dims->data[1],
    //          input->dims->data[2],
    //          input->dims->data[3]);
    //Serial.printf("Input bytes: %d\n", input->bytes);


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
  
  }

  // Обнулить оценку уровня шума. Используется для детекции речи/голоса (VAD).
  smoothed_noise_floor = 0.0f;

  // Освободить выделенную память под спектрограмму.
  free_spectrogram(spec, frames);
}
