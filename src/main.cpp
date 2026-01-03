#include <Arduino.h>
#include <atomic>
// 引脚定义
#define ZERO_CROSS_PIN 45 // 双向光耦输入引脚
#define TRIAC_PIN 48      // 可控硅触发引脚

std::atomic<int64_t> predictedTime(0);
std::atomic<bool> hasNewSignal(false);

// 更新信号历史并预测下一个信号时间
void updateSignalHistory(int64_t interval, int64_t currentTime)
{
  static int8_t intervalHistory[50] = {};
  static int8_t currentIndex = 0;
  static int8_t historySize = 0;

  // 更新历史记录大小（最大50）
  if (historySize < 50)
  {
    historySize++;
  }

  // 存储当前间隔时间
  intervalHistory[currentIndex] = interval;

  // 循环更新索引
  currentIndex++;
  if (currentIndex > 49)
  {
    currentIndex = 0;
  }

  // 计算平均间隔时间
  int32_t totalInterval = 0;
  for (size_t i = 0; i < historySize; i++)
  {
    totalInterval += intervalHistory[i];
  }
  int averageInterval = totalInterval / historySize;
  int64_t value = currentTime + averageInterval;

  // 存储预测时间
  predictedTime.store(value, std::memory_order_relaxed);
  hasNewSignal.store(true, std::memory_order_release);
}

// 中断服务程序：处理信号并记录时间间隔
void IRAM_ATTR handleSignalInterrupt()
{
  int64_t currentTime = esp_timer_get_time();
  static int64_t previousTime = 0;

  unsigned int interval = currentTime - previousTime;

  // 防抖处理：只处理5-15ms内的信号
  if (interval > 5000 && interval < 15000)
  {
    updateSignalHistory(interval, currentTime);
  }

  previousTime = currentTime;
}

TaskHandle_t CLHandle = NULL;
// 过零触发任务
void zeroCrossingControlTask(void *parameter)
{
  float percent = 1;
  float del = 5;

  int last = 0;
  while (1)
  {
    // 过零检查
    if (hasNewSignal.exchange(false, std::memory_order_acq_rel))
    {
      int64_t predictedTimeValue = predictedTime.load(std::memory_order_relaxed);
      int timeUntilTrigger = predictedTimeValue - esp_timer_get_time();

      // 让出CPU
      if (timeUntilTrigger > 2000)
      {
        int delayMilliseconds = (timeUntilTrigger - 1000) / 1000;
        vTaskDelay(pdMS_TO_TICKS(delayMilliseconds));
      }

      timeUntilTrigger = predictedTimeValue - esp_timer_get_time();
      timeUntilTrigger += 900; // 补偿处理延迟
      // 映射有效值
      int v1 = 10000 * acos(1 - 2 * (100 - percent) / 100) / PI;
      timeUntilTrigger += v1;
      if (timeUntilTrigger > 0 && timeUntilTrigger < 10000)
      {
        esp_rom_delay_us(timeUntilTrigger);

        // 触发可控硅导通
        digitalWrite(TRIAC_PIN, HIGH);
        esp_rom_delay_us(100); // 触发脉冲宽度
        digitalWrite(TRIAC_PIN, LOW);
      }

        percent += del;
      if (percent > 95 || percent < 0)
      {
        del = -del;
        percent += del * 2;
      }



      // 调试输出
      Serial.print(percent);
      Serial.print("% | ");
      Serial.println(timeUntilTrigger);
    }
  }
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=== ESP32-S3 过零检测测试 ===");

  pinMode(TRIAC_PIN, OUTPUT);
  digitalWrite(TRIAC_PIN, LOW);
  pinMode(ZERO_CROSS_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ZERO_CROSS_PIN), handleSignalInterrupt, RISING);

  xTaskCreatePinnedToCore(
      zeroCrossingControlTask, // 任务函数
      "1",                     // 任务名称
      1024 * 3,                // 堆栈大小（字节）
      NULL,                    // 参数
      10,                      // 优先级
      &CLHandle,               // 任务句柄
      1                        // 核心编号
  );
}

void loop()
{
  vTaskDelete(NULL);
}