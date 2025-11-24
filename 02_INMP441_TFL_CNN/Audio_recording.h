#include <driver/i2s.h>

// --- Настройка пинов I2S ---
#define I2S_WS   16         // LRCL (word select, left/right clock)
#define I2S_SD   8          // DOUT (data)
#define I2S_SCK  9          // BCLK (bit clock)
#define I2S_PORT I2S_NUM_0  // номер I2S интерфейса (I2S_NUM_0 / I2S_NUM_1) 

// --- Настройки записи ---
#define SAMPLE_RATE     16000           // Частота дискретизации
#define BITS_PER_SAMPLE 16              // Глубина квантования (бит)
#define CHANNELS        1               // Только левый канал
#define RECORD_TIME     1               // Длительность записи в секундах

// --- Размер одного сэмпла ---
#define BYTES_PER_SAMPLE (BITS_PER_SAMPLE / 8)

// --- Расчёт длины данных ---
#define SAMPLES_COUNT  (SAMPLE_RATE * RECORD_TIME)         // Размер дискретизоного аудиосигнала (16000 точек).
#define DATA_SIZE      (SAMPLES_COUNT * BYTES_PER_SAMPLE)  // Размер аудио-данных в байтах (не включая заголовок).
#define WAV_HEADER_SIZE 44                                 // Размер заголовка в байтах.

// --- Буфер для хранения WAV: заголовок (44 байта) + данные ---
uint8_t wav_buffer[WAV_HEADER_SIZE + DATA_SIZE];

// --- Временный буфер для чтения из I2S ---
int16_t i2s_buffer[256]; // промежуточный буфер


// ===============================
// Инициализация I2S
// ===============================
void i2s_install() {
  // Создаём локальную переменную для агрегатной инициализации.
  const i2s_config_t i2s_config = {
    // Указываем режим работы I2S: 
    // - I2S_MODE_MASTER  — ESP32 выступает мастером (генерирует тактовые сигналы BCLK и LRCLK/WS).
    // - I2S_MODE_RX      — режим приёма (read) — мы читаем данные с внешнего устройства (микрофона).
    .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX),       // Master, приём данных
    // Частота дискретизации 16000 Гц.
    .sample_rate = SAMPLE_RATE,
    // Число бит на выборку.
    .bits_per_sample = i2s_bits_per_sample_t(BITS_PER_SAMPLE),
    // Формат каналов: используем только левый канал.
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    // Формат протокола передачи: стандартный I2S.
    .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
    // Флаги выделения прерываний (interrupt allocation flags): 0 - использовать приоритет по умолчанию.
    .intr_alloc_flags = 0,
    // Количество DMA-буферов (кольцевой буфер для DMA). Чем больше — тем более гладкая передача, но тем больше ОЗУ расходуется.
    .dma_buf_count = 8,
    // Длина каждого DMA буфера в *сэмплах*.
    .dma_buf_len = 256,
    // Флаг использования APLL (audio PLL).
    .use_apll = false
  };
  // Вызов функции инициализации I2S-драйвера с указанной конфигурацией.
  // Параметры:
  // - I2S_PORT: номер порта (I2S_NUM_0 или I2S_NUM_1).
  // - &i2s_config: указатель на структуру конфигурации.
  // - 0: size of install queue (если нужен callback/ISR queue, можно задать >0).
  // - NULL: указатель для handle очереди событий (если третий параметр > 0, сюда вернётся хэндл).
  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
}


// ===============================
// Настройка пинов I2S
// ===============================
void i2s_setpin() {
  // Инициализируем структуру i2s_pin_config_t чтобы задать, какие GPIO будут использоваться для сигналов I2S.
  const i2s_pin_config_t pin_config = {
    // BCK (Bit Clock) — основной тактовый сигнал, который тактует отдельные биты каждой выборки.
    .bck_io_num = I2S_SCK,
    // WS меняет своё состояние раз в выборку/кадр и разделяет каналы (левый/правый) для стерео. Для моно-микрофона WS всё ещё нужен как «граница сэмпла».
    .ws_io_num = I2S_WS,
    // GPIO для передачи данных (TX). Устанавливаем -1, чтобы отключить линию передачи.
    .data_out_num = -1, // не используем передачу
    // GPIO для приёма данных (RX). Этот пин будет читать последовательность байт/бит от микрофона.
    .data_in_num = I2S_SD
  };
  // Применяем конфигурацию к указанному I2S порту.
  // Параметры:
  // - I2S_PORT: номер порта (I2S_NUM_0 или I2S_NUM_1).
  // - &pin_config: указатель на структуру конфигурации.
  i2s_set_pin(I2S_PORT, &pin_config);
}

// ===============================
// Формирование WAV-заголовка (44 байта) и запись его в буфер header. Заголовок описывает аудиоданные длиной data_size байт.
// - uint8_t *header: указатель на буфер, куда будет записан заголовок, а в дальнейшем аудио.
// - uint32_t data_size: размер аудио-данных в байтах (не включая заголовок).
// ===============================
void create_wav_header(uint8_t *header, uint32_t data_size) {
  // Значение, которое записывается в поле "ChunkSize" заголовка RIFF.
  uint32_t file_size = data_size + 36;

  // Копируем первые четыре байта "RIFF" (ASCII) в начало заголовка.
  memcpy(header, "RIFF", 4);
  // Записываем ChunkSize (4 байта, little-endian)
  header[4] = (file_size) & 0xFF;
  header[5] = (file_size >> 8) & 0xFF;
  header[6] = (file_size >> 16) & 0xFF;
  header[7] = (file_size >> 24) & 0xFF;

  // Копируем следующую метку 8 байтов: "WAVEfmt ".
  memcpy(header + 8, "WAVEfmt ", 8);

  // Размер подблока fmt = 16
  header[16] = 16; header[17] = 0; header[18] = 0; header[19] = 0;
  // Аудио формат 1 (PCM)
  header[20] = 1; header[21] = 0;
  // Количество каналов
  header[22] = CHANNELS; header[23] = 0;
  // Частота дискретизации
  header[24] = SAMPLE_RATE & 0xFF;
  header[25] = (SAMPLE_RATE >> 8) & 0xFF;
  header[26] = (SAMPLE_RATE >> 16) & 0xFF;
  header[27] = (SAMPLE_RATE >> 24) & 0xFF;
  // Байтов в секунду = sampleRate * channels * bytesPerSample
  uint32_t byte_rate = SAMPLE_RATE * CHANNELS * BYTES_PER_SAMPLE;
  header[28] = byte_rate & 0xFF;
  header[29] = (byte_rate >> 8) & 0xFF;
  header[30] = (byte_rate >> 16) & 0xFF;
  header[31] = (byte_rate >> 24) & 0xFF;
  // Размер блока = channels * bytesPerSample
  uint16_t block_align = CHANNELS * BYTES_PER_SAMPLE;
  header[32] = block_align & 0xFF;
  header[33] = (block_align >> 8) & 0xFF;
  // Бит на сэмпл
  header[34] = BITS_PER_SAMPLE; header[35] = 0;
  // Подблок data
  memcpy(header + 36, "data", 4);
  header[40] = data_size & 0xFF;
  header[41] = (data_size >> 8) & 0xFF;
  header[42] = (data_size >> 16) & 0xFF;
  header[43] = (data_size >> 24) & 0xFF;
}





// ===============================
// Функция в течение RECORD_TIME секунд читает аудиоданные из I2S-интерфейса (микрофон INMP441) и последовательно 
// записывает их в заранее выделённый буфер wav_buffer, начиная с позиции сразу после WAV-заголовка.
// ===============================
void record_to_buffer() {
  // Переменная запоминает сколько байт прочитано последним вызовом i2s_read.
  size_t bytes_read = 0;

  // Указатель на место в массиве wav_buffer, откуда начнём записывать данные.
  //  - wav_buffer — глобальный массив.
  //  - Пропустить первые WAV_HEADER_SIZE байт (заголовок) и записывать данные сразу после него.
  uint8_t *write_ptr = wav_buffer + WAV_HEADER_SIZE;

  // Счётчик, сколько байт всего уже записано в буфер (начинаем с 0).
  size_t bytes_written = 0;

  // Момент времени начала записи аудио с микрофона.
  uint32_t start = millis();
  // Выполняем чтение из I2S до тех пор, пока не истечёт RECORD_TIME секунд.
  while (millis() - start < RECORD_TIME * 1000) {
    // Читаем данные из I2S-порта:
    //  - I2S_PORT — номер I2S интерфейса (I2S_NUM_0 / I2S_NUM_1) — глобальная константа.
    //  - (void*)i2s_buffer — указатель на временный буфер (глобальный/локальный массив) для чтения.
    //  - sizeof(i2s_buffer) — максимальное число байт для чтения за один вызов.
    //  - &bytes_read — результат: реальное число считанных байт.
    //  - portMAX_DELAY — блокировка до тех пор, пока данные не появятся (ожидаем бесконечно).
    // Функция возвращает esp_err_t: ESP_OK при успешном чтении.
    esp_err_t result = i2s_read(I2S_PORT, (void*)i2s_buffer, sizeof(i2s_buffer), &bytes_read, portMAX_DELAY);

    // Проверяем, что чтение прошло успешно и реально прочитаны байты.
    if (result == ESP_OK && bytes_read > 0) {
      // Проверяем, хватит ли места в нашем глобальном буфере (wav_buffer) для этих bytes_read байт.
      // DATA_SIZE — максимальное количество байт для аудиоданных, выделенное в wav_buffer (глобальная).
      if (bytes_written + bytes_read <= DATA_SIZE) {
        // Копируем bytes_read байт из временного буфера i2s_buffer в wav_buffer по указателю write_ptr.
        memcpy(write_ptr, i2s_buffer, bytes_read);
        // 10) Сдвигаем указатель write_ptr дальше на количество записанных байт, чтобы следующий memcpy записал данные после уже записанных.
        write_ptr += bytes_read;
        // Увеличиваем общий счётчик записанных байт.
        bytes_written += bytes_read;
      } else {
        // Если места не хватает (попытка переписать DATA_SIZE), то выходим из цикла.
        break;
      }
    }
  }

  //Serial.printf("Captured %u bytes of audio data.\n", bytes_written);
}






// ===============================
// Функция увеличивает громкость аудио.
//  - uint8_t * d_buff: Указатель на исходное тихое аудио.
//  - uint8_t* s_buff: Указатель для запизи громкого аудио.
//  - uint32_t len: размер аудио-данных в байтах (не включая заголовок).
// ===============================
void audio_scale(uint8_t * d_buff, uint8_t* s_buff, uint32_t len)
{
    uint32_t j = 0;              // Индекс для записи в выходной буфер.
    uint32_t dac_value = 0;      // Раскодированное значение одной 12-битной выборки.

    // Пройдём по всем байтам аудиосигнала. 
    // Читаем каждые 2 байта входа — одна 12-битная выборка I2S (инвертированная упаковка INMP441).
    for (int i = 0; i < len; i += 2) {
      // Восстанавливаем 12-битное значение: нижний байт + 4 бита из старшего байта.
      dac_value = ((((uint16_t) (s_buff[i + 1] & 0xf) << 8) | ((s_buff[i + 0]))));

      // Старший байт 16-битного PCM = 0 (младший байт записывается далее). Little-endian формат.
      d_buff[j++] = 0;

      // Масштабируем 12-бит (0..4095) в диапазон ~0..511 и записываем младший байт результата.
      d_buff[j++] = dac_value * 256 / 2048;
    }
}