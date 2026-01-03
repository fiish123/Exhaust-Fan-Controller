#include <sensor.h>

// 创建温湿度传感器对象
SensirionI2cSht4x sht4x;
// 创建SGP41气体传感器对象
SensirionI2CSgp41 sgp41;

// 创建VOC气体指数算法对象
VOCGasIndexAlgorithm voc_algorithm;
// 创建NOx气体指数算法对象
NOxGasIndexAlgorithm nox_algorithm;

// NOx传感器初始化所需时间（秒）
uint16_t conditioning_s = 10;

// 错误信息存储数组
char errorMessage[256];

bool init_sensor()
{

    // 初始化I2C总线
    Wire.begin();

    // 初始化SHT40温湿度传感器，使用地址0x44
    sht4x.begin(Wire, SHT45_I2C_ADDR_44);
    // 初始化SGP41气体传感器
    sgp41.begin(Wire);

    // 等待1秒确保串口完全就绪
    delay(1000);

    // 传感器自检
    uint16_t testResult;
    uint16_t error;
    error = sgp41.executeSelfTest(testResult);
    if (error)
    {
        Serial.print("自检executeSelfTest()执行错误 : ");
        errorToString(error, errorMessage, 256);
        Serial.println(errorMessage);
    }
    else if (testResult != 0xD400)
    {
        Serial.print("自检错误: ");
        Serial.println(testResult);
    }

    if (error)
    {
        Wire.end();
        return false;
    }

    // 定义VOC算法参数变量
    int32_t index_offset;
    int32_t learning_time_offset_hours;
    int32_t learning_time_gain_hours;
    int32_t gating_max_duration_minutes;
    int32_t std_initial;
    int32_t gain_factor;

    // 获取VOC算法的调优参数
    voc_algorithm.get_tuning_parameters(
        index_offset, learning_time_offset_hours, learning_time_gain_hours,
        gating_max_duration_minutes, std_initial, gain_factor);

    // 打印VOC算法参数到串口
    Serial.println("\nVOC气体指数算法参数");
    Serial.print("指数偏移量:\t");
    Serial.println(index_offset);
    Serial.print("学习时间偏移量（小时）:\t");
    Serial.println(learning_time_offset_hours);
    Serial.print("学习时间增益（小时）:\t");
    Serial.println(learning_time_gain_hours);
    Serial.print("门控最大持续时间（分钟）:\t");
    Serial.println(gating_max_duration_minutes);
    Serial.print("初始标准差:\t");
    Serial.println(std_initial);
    Serial.print("增益因子:\t");
    Serial.println(gain_factor);

    // 获取NOx算法的调优参数
    nox_algorithm.get_tuning_parameters(
        index_offset, learning_time_offset_hours, learning_time_gain_hours,
        gating_max_duration_minutes, std_initial, gain_factor);

    // 打印NOx算法参数到串口
    Serial.println("\nNOx气体指数算法参数");
    Serial.print("指数偏移量:\t");
    Serial.println(index_offset);
    Serial.print("学习时间偏移量（小时）:\t");
    Serial.println(learning_time_offset_hours);
    Serial.print("门控最大持续时间（分钟）:\t");
    Serial.println(gating_max_duration_minutes);
    Serial.print("增益因子:\t");
    Serial.println(gain_factor);
    Serial.println("");

    return true;
}

void test()
{
    uint16_t error;
    float humidity = 0;    // 湿度（%RH）
    float temperature = 0; // 温度（摄氏度）
    uint16_t srawVoc = 0;  // VOC原始信号值
    uint16_t srawNox = 0;  // NOx原始信号值

    // 默认温湿度补偿值（SGP41定义的刻度格式）
    uint16_t defaultCompenstaionRh = 0x8000; // 湿度补偿默认值
    uint16_t defaultCompenstaionT = 0x6666;  // 温度补偿默认值
    uint16_t compensationRh = 0;             // 实际湿度补偿值
    uint16_t compensationT = 0;              // 实际温度补偿值

    // 1. 延迟1秒，满足气体指数算法的1Hz采样要求
    delay(1000);

    // 2. 测量温湿度用于SGP41内部补偿
    error = sht4x.measureHighPrecision(temperature, humidity);
    if (error)
    {
        // 如果温湿度测量失败，打印错误信息
        Serial.print("SHT4x - 执行measureHighPrecision()时出错: ");
        errorToString(error, errorMessage, 256);
        Serial.println(errorMessage);
        Serial.println("使用默认温湿度补偿值进行SGP41补偿");

        // 使用默认补偿值
        compensationRh = defaultCompenstaionRh;
        compensationT = defaultCompenstaionT;
    }
    else
    {
        // 成功获取温湿度数据
        Serial.print("温度:");
        Serial.print(temperature);
        Serial.print("℃\t");
        Serial.print("湿度:");
        Serial.print(humidity);
        Serial.println("%RH");

        // 将温湿度转换为SGP41要求的刻度格式
        // 温度转换公式：(-45°C ~ 130°C)映射到0~65535
        compensationT = static_cast<uint16_t>((temperature + 45) * 65535 / 175);
        // 湿度转换公式：(0% ~ 100%RH)映射到0~65535
        compensationRh = static_cast<uint16_t>(humidity * 65535 / 100);
    }

    // 3. 测量SGP41传感器原始信号
    if (conditioning_s > 0)
    {
        // NOx传感器初始化阶段（前10秒）
        // 此时srawNox保持为0
        error = sgp41.executeConditioning(compensationRh, compensationT, srawVoc);
        conditioning_s--; // 减少剩余初始化时间
    }
    else
    {
        // 初始化完成后进行正常测量
        error = sgp41.measureRawSignals(compensationRh, compensationT, srawVoc, srawNox);
    }

    // 4. 使用气体指数算法处理原始信号，获取VOC和NOx指数
    if (error)
    {
        // 如果气体传感器测量失败，打印错误信息
        Serial.print("SGP41 - 执行measureRawSignals()时出错: ");
        errorToString(error, errorMessage, 256);
        Serial.println(errorMessage);
    }
    else
    {
        // 成功获取原始信号，计算气体指数
        int32_t voc_index = voc_algorithm.process(srawVoc);
        int32_t nox_index = nox_algorithm.process(srawNox);

        // 打印VOC和NOx指数
        Serial.print("VOC指数: ");
        Serial.print(voc_index);
        Serial.print("\t");
        Serial.print("NOx指数: ");
        Serial.println(nox_index);
    }
}