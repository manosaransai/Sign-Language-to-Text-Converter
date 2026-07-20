/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdio.h>
#include <math.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;

I2C_HandleTypeDef hi2c1;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
UART_HandleTypeDef huart3;

/* USER CODE BEGIN PV */

// ---- Flex sensor ADC buffer (5 channels via DMA) ----
uint32_t adc_buf[5];

// ---- MPU6050 raw accelerometer values ----
int16_t ax, ay, az;

// ---- AI input/output arrays ----
float ai_in[7];

// ---- Letter detection variables ----
char prev_letter = ' ';
uint32_t stable_start = 0;
uint8_t letter_sent = 0;

// ---- Word buffer ----
char word_buf[32];
uint8_t word_len = 0;

// ---- UART message buffer ----
char msg[160];

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_ADC1_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_USART3_UART_Init(void);
/* USER CODE BEGIN PFP */
void uart_print(const char *str);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

// =============================================
// MPU6050 — Wake up (write 0x00 to PWR_MGMT_1)
// =============================================
void MPU6050_Init(void)
{
    uint8_t data = 0x00;
    HAL_I2C_Mem_Write(&hi2c1, 0x68 << 1, 0x6B,
                      I2C_MEMADD_SIZE_8BIT,
                      &data, 1, 100);
    HAL_Delay(100);
}

// =============================================
// MPU6050 — Read 6 bytes starting at 0x3B
// Fills ax, ay, az globals
// =============================================
void MPU6050_Read(void)
{
    uint8_t buf[6];
    HAL_I2C_Mem_Read(&hi2c1, 0x68 << 1, 0x3B,
                     I2C_MEMADD_SIZE_8BIT,
                     buf, 6, 100);
    ax = (int16_t)((buf[0] << 8) | buf[1]);
    ay = (int16_t)((buf[2] << 8) | buf[3]);
    az = (int16_t)((buf[4] << 8) | buf[5]);
}

// =============================================
// DFPlayer — Play track number (1 = A, 2 = B...)
// =============================================
void DFPlayer_Play(uint8_t track)
{
    uint8_t cmd[10];
    cmd[0] = 0x7E;
    cmd[1] = 0xFF;
    cmd[2] = 0x06;
    cmd[3] = 0x03;
    cmd[4] = 0x00;
    cmd[5] = 0x00;
    cmd[6] = track;

    int16_t checksum = 0;
    for (int i = 1; i <= 6; i++)
        checksum += cmd[i];
    checksum = -checksum;

    cmd[7] = (checksum >> 8) & 0xFF;
    cmd[8] = checksum & 0xFF;
    cmd[9] = 0xEF;

    HAL_UART_Transmit(&huart3, cmd, 10, 200);
}

// =============================================
// DFPlayer — Set volume (0 to 30)
// =============================================
void DFPlayer_SetVolume(uint8_t vol)
{
    uint8_t cmd[10];
    cmd[0] = 0x7E;
    cmd[1] = 0xFF;
    cmd[2] = 0x06;
    cmd[3] = 0x06;
    cmd[4] = 0x00;
    cmd[5] = 0x00;
    cmd[6] = vol;

    int16_t checksum = 0;
    for (int i = 1; i <= 6; i++)
        checksum += cmd[i];
    checksum = -checksum;

    cmd[7] = (checksum >> 8) & 0xFF;
    cmd[8] = checksum & 0xFF;
    cmd[9] = 0xEF;

    HAL_UART_Transmit(&huart3, cmd, 10, 200);
}

// =============================================
// BLE Send — sends string to HM-10 via UART2
// =============================================
void BLE_Send(const char *str)
{
    HAL_UART_Transmit(&huart2,
                      (uint8_t*)str,
                      strlen(str), 500);
}

// =============================================
// uart_print — sends debug string to PuTTY via UART2
// Open PuTTY: Serial, COM port, 9600 baud
// NOTE: Renamed from DEBUG to avoid conflict with
//       the -DDEBUG compiler flag set by STM32CubeIDE
// =============================================
void uart_print(const char *str)
{
    HAL_UART_Transmit(&huart2,
                      (uint8_t*)str,
                      strlen(str), 500);
}

// =============================================
// SIMPLE THRESHOLD GESTURE DETECT
// =============================================
char Simple_Predict(void)
{
    float f0 = adc_buf[0] / 4095.0f;  // Thumb
    float f1 = adc_buf[1] / 4095.0f;  // Index
    float f2 = adc_buf[2] / 4095.0f;  // Middle
    float f3 = adc_buf[3] / 4095.0f;  // Ring
    float f4 = adc_buf[4] / 4095.0f;  // Pinky

    // A — all fingers bent, thumb to side
    if (f0 < 0.4f && f1 > 0.7f && f2 > 0.7f &&
        f3 > 0.7f && f4 > 0.7f)
        return 'A';

    // B — all fingers up straight, thumb bent
    if (f0 > 0.7f && f1 < 0.3f && f2 < 0.3f &&
        f3 < 0.3f && f4 < 0.3f)
        return 'B';

    // C — all fingers curved
    if (f0 < 0.6f && f1 < 0.6f && f2 < 0.6f &&
        f3 < 0.6f && f4 < 0.6f &&
        f0 > 0.2f && f1 > 0.2f)
        return 'C';

    // D — index up, others bent
    if (f1 < 0.3f && f2 > 0.7f &&
        f3 > 0.7f && f4 > 0.7f)
        return 'D';

    return '?';
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC1_Init();
  MX_I2C1_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_USART3_UART_Init();

  /* USER CODE BEGIN 2 */

  MPU6050_Init();
  HAL_ADC_Start_DMA(&hadc1, adc_buf, 5);
  HAL_Delay(1500);
  DFPlayer_SetVolume(25);
  HAL_Delay(200);

  // ---- Startup messages — visible in PuTTY ----
  uart_print("=== SignLang Boot OK ===\r\n");
  uart_print("Flex 0-4 | Ax Ay Az | Letter\r\n");
  uart_print("-------------------------------\r\n");

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */
    /* USER CODE BEGIN WHILE */

    prev_letter = ' ';
    stable_start = HAL_GetTick();
    letter_sent = 0;
    word_len = 0;
    memset(word_buf, 0, sizeof(word_buf));

    while (1)
    {
        // ---- Read MPU6050 ----
        MPU6050_Read();

        // ---- Print all sensor values to PuTTY ----
        sprintf(msg,
            "F:%4lu %4lu %4lu %4lu %4lu | "
            "A:%6d %6d %6d\r\n",
            adc_buf[0], adc_buf[1], adc_buf[2],
            adc_buf[3], adc_buf[4],
            ax, ay, az);
        uart_print(msg);

        // ---- Run gesture prediction ----
        char letter = Simple_Predict();

        // ---- Stability check: same sign held 400ms ----
        if (letter != prev_letter)
        {
            prev_letter = letter;
            stable_start = HAL_GetTick();
            letter_sent = 0;
        }

        if (!letter_sent &&
            letter != '?' &&
            (HAL_GetTick() - stable_start) > 400)
        {
            letter_sent = 1;

            // --- 1. Print detected letter to PuTTY ---
            sprintf(msg, ">>> LETTER: %c\r\n", letter);
            uart_print(msg);

            // --- 2. Play audio on speaker ---
            DFPlayer_Play(letter - 'A' + 1);

            // --- 3. Send letter over BLE to phone ---
            sprintf(msg, "Letter: %c\r\n", letter);
            BLE_Send(msg);

            // --- 4. Build word buffer ---
            if (word_len < 31)
            {
                word_buf[word_len] = letter;
                word_len++;
                word_buf[word_len] = '\0';
            }

            // Print current word to PuTTY
            sprintf(msg, "    Word so far: [%s]\r\n", word_buf);
            uart_print(msg);

            // Send word to BLE phone
            sprintf(msg, "Word: %s\r\n", word_buf);
            BLE_Send(msg);
        }

        // ---- USER BUTTON on PC13 = confirm word ----
        if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_RESET)
        {
            if (word_len > 0)
            {
                sprintf(msg,
                    "\r\n=== WORD CONFIRMED: [%s] ===\r\n\r\n",
                    word_buf);
                uart_print(msg);

                sprintf(msg, "DONE: %s\r\n", word_buf);
                BLE_Send(msg);
            }

            memset(word_buf, 0, sizeof(word_buf));
            word_len = 0;

            HAL_Delay(500);
        }

        HAL_Delay(50);
    }

    /* USER CODE END WHILE */
    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 180;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_PWREx_EnableOverDrive() != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  */
static void MX_ADC1_Init(void)
{
  ADC_AnalogWDGConfTypeDef AnalogWDGConfig = {0};
  ADC_ChannelConfTypeDef sConfig = {0};

  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = ENABLE;
  hadc1.Init.ContinuousConvMode = ENABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 5;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  AnalogWDGConfig.WatchdogMode = ADC_ANALOGWATCHDOG_SINGLE_REG;
  AnalogWDGConfig.HighThreshold = 0;
  AnalogWDGConfig.LowThreshold = 0;
  AnalogWDGConfig.Channel = ADC_CHANNEL_0;
  AnalogWDGConfig.ITMode = DISABLE;
  if (HAL_ADC_AnalogWDGConfig(&hadc1, &AnalogWDGConfig) != HAL_OK)
  {
    Error_Handler();
  }

  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  sConfig.Channel = ADC_CHANNEL_1;
  sConfig.Rank = 2;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  sConfig.Channel = ADC_CHANNEL_4;
  sConfig.Rank = 3;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  sConfig.Channel = ADC_CHANNEL_8;
  sConfig.Rank = 4;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  sConfig.Channel = ADC_CHANNEL_10;
  sConfig.Rank = 5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  */
static void MX_I2C1_Init(void)
{
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART1 Initialization Function
  */
static void MX_USART1_UART_Init(void)
{
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART2 Initialization Function
  */
static void MX_USART2_UART_Init(void)
{
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART3 Initialization Function
  */
static void MX_USART3_UART_Init(void)
{
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 9600;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{
  __HAL_RCC_DMA2_CLK_ENABLE();
  HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);
}

/**
  * @brief GPIO Initialization Function
  */
static void MX_GPIO_Init(void)
{
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* User can add his own implementation */
}
#endif /* USE_FULL_ASSERT */
