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
#include <stdint.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum {
  MODE_IDLE = 0,      /* 待機：等待 UART 指令          */
  MODE_BANANA,        /* 黃燈閃爍 + 蜂鳴器呼吸音量      */
  MODE_WATERMELON,    /* 綠燈閃爍 + 蜂鳴器呼吸音量      */
  MODE_RED            /* 紅燈恆亮，其他燈與蜂鳴器關閉   */
} AppMode_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* ---- LED 腳位 ---- */
#define LED_PORT            GPIOC
#define LED_RED_PIN         GPIO_PIN_8   /* 紅燈 PC8 */
#define LED_YELLOW_PIN      GPIO_PIN_6   /* 黃燈 PC6 */
#define LED_GREEN_PIN       GPIO_PIN_7   /* 綠燈 PC7 */
#define LED_ALL_PINS        (LED_RED_PIN | LED_YELLOW_PIN | LED_GREEN_PIN)

/* ---- 按鈕 B1：PA11，上拉，上升緣中斷 ---- */
#define BTN_PORT            GPIOA
#define BTN_PIN             GPIO_PIN_11

/* ---- 蜂鳴器：TIM3 CH4，170MHz/(169+1)=1MHz，ARR=999 → 1kHz ---- */
#define BUZZER_CHANNEL      TIM_CHANNEL_4
#define BUZZER_MAX_DUTY     500U      /* 50% duty = 最大音量 */

/* ---- 時間參數 (ms) ---- */
#define RUN_TIME_MS         10000U    /* 動作維持 10 秒          */
#define BREATH_PERIOD_MS    3000U     /* 呼吸一個循環 3 秒       */
#define BLINK_HALF_MS       250U      /* LED 亮 250ms / 滅 250ms */
#define DEBOUNCE_MS         50U       /* 按鈕防彈跳 50ms         */
#define DOUBLE_CLICK_MS     500U      /* 兩下需在 500ms 內       */

/* ---- 緩衝區大小 ---- */
#define RX_RING_SIZE        128U      /* 必須是 2 的次方         */
#define LINE_BUF_SIZE       32U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef hlpuart1;

TIM_HandleTypeDef htim3;

/* USER CODE BEGIN PV */
/* ---- Ring buffer（UART 中斷寫入，主迴圈讀出） ---- */
static uint8_t           rxRing[RX_RING_SIZE];
static volatile uint16_t rxHead = 0;
static volatile uint16_t rxTail = 0;
static volatile uint8_t  rxOverflow = 0;
static uint8_t           rxByte;

/* ---- Line buffer ---- */
static char     lineBuf[LINE_BUF_SIZE];
static uint16_t lineLen = 0;
static uint8_t  lineTooLong = 0;

/* ---- 狀態機 ---- */
static AppMode_t g_mode = MODE_IDLE;
static uint32_t  g_modeStartTick = 0;

/* ---- 按鈕 ---- */
static GPIO_PinState     btnIdleLevel = GPIO_PIN_SET;  /* 開機自動偵測 */
static volatile uint8_t  btnEdgePending = 0;           /* 中斷設定     */
static volatile uint32_t btnLastEdgeTick = 0;          /* 中斷設定     */
static uint8_t           btnClickCount = 0;
static uint32_t          btnFirstClickTick = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_LPUART1_UART_Init(void);
static void MX_TIM3_Init(void);
/* USER CODE BEGIN PFP */
static void    UART_Print(const char *s);
static uint8_t RxRing_Put(uint8_t c);
static uint8_t RxRing_Get(uint8_t *c);
static void    Line_Process(void);
static void    Command_Handle(const char *cmd);
static void    App_StartMode(AppMode_t mode);
static void    App_EnterRed(const char *reason);
static void    App_Task(void);
static void    Button_DetectIdleLevel(void);
static void    Button_Task(void);
static void    Button_OnClick(uint32_t now);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_LPUART1_UART_Init();
  MX_TIM3_Init();
  /* USER CODE BEGIN 2 */
  /* PWM 先以 0% duty 啟動，之後只改 CCR */
  __HAL_TIM_SET_COMPARE(&htim3, BUZZER_CHANNEL, 0U);
  HAL_TIM_PWM_Start(&htim3, BUZZER_CHANNEL);

  HAL_GPIO_WritePin(LED_PORT, LED_ALL_PINS, GPIO_PIN_RESET);

  UART_Print("\r\nReady. Commands: banana / wattermalon\r\n");
  Button_DetectIdleLevel();               /* 自動偵測按鈕閒置電位 */

  btnEdgePending = 0U;                    /* 清掉開機時可能的雜訊 */
  HAL_UART_Receive_IT(&hlpuart1, &rxByte, 1);
  UART_Print("> ");
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    Line_Process();   /* ring buffer → line buffer → 指令 */
    Button_Task();    /* 確認中斷抓到的按鈕事件（50ms 防彈跳） */
    App_Task();       /* 閃燈 / 呼吸音量 / 10 秒計時 */
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

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV4;
  RCC_OscInitStruct.PLL.PLLN = 85;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief LPUART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_LPUART1_UART_Init(void)
{

  /* USER CODE BEGIN LPUART1_Init 0 */

  /* USER CODE END LPUART1_Init 0 */

  /* USER CODE BEGIN LPUART1_Init 1 */

  /* USER CODE END LPUART1_Init 1 */
  hlpuart1.Instance = LPUART1;
  hlpuart1.Init.BaudRate = 115200;
  hlpuart1.Init.WordLength = UART_WORDLENGTH_8B;
  hlpuart1.Init.StopBits = UART_STOPBITS_1;
  hlpuart1.Init.Parity = UART_PARITY_NONE;
  hlpuart1.Init.Mode = UART_MODE_TX_RX;
  hlpuart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  hlpuart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  hlpuart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  hlpuart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&hlpuart1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&hlpuart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&hlpuart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&hlpuart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN LPUART1_Init 2 */

  /* USER CODE END LPUART1_Init 2 */

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 169;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 999;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_4) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */
  HAL_TIM_MspPostInit(&htim3);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_6|GPIO_PIN_7|GPIO_PIN_8, GPIO_PIN_RESET);

  /*Configure GPIO pins : PC6 PC7 PC8 */
  GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_7|GPIO_PIN_8;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : PA11 */
  GPIO_InitStruct.Pin = GPIO_PIN_11;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* ======================= UART 輸出 ======================= */
static void UART_Print(const char *s)
{
  HAL_UART_Transmit(&hlpuart1, (uint8_t *)s, (uint16_t)strlen(s), 100);
}

/* ======================= Ring buffer ======================= */
/* 只在 UART 中斷呼叫（單一生產者） */
static uint8_t RxRing_Put(uint8_t c)
{
  uint16_t next = (uint16_t)((rxHead + 1U) & (RX_RING_SIZE - 1U));
  if (next == rxTail)
  {
    return 0U;                 /* 滿了，丟棄 */
  }
  rxRing[rxHead] = c;
  rxHead = next;
  return 1U;
}

/* 只在主迴圈呼叫（單一消費者） */
static uint8_t RxRing_Get(uint8_t *c)
{
  if (rxTail == rxHead)
  {
    return 0U;                 /* 空的 */
  }
  *c = rxRing[rxTail];
  rxTail = (uint16_t)((rxTail + 1U) & (RX_RING_SIZE - 1U));
  return 1U;
}

/* ======================= UART 中斷 callback ======================= */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == LPUART1)
  {
    if (!RxRing_Put(rxByte))
    {
      rxOverflow = 1U;
    }
    HAL_UART_Receive_IT(&hlpuart1, &rxByte, 1);   /* 重新啟動接收 */
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == LPUART1)
  {
    __HAL_UART_CLEAR_FLAG(huart, UART_CLEAR_OREF | UART_CLEAR_NEF |
                                 UART_CLEAR_FEF  | UART_CLEAR_PEF);
    HAL_UART_Receive_IT(&hlpuart1, &rxByte, 1);
  }
}

/* ======================= Line buffer ======================= */
static void Line_Process(void)
{
  uint8_t c;

  if (rxOverflow)
  {
    rxOverflow = 0U;
    UART_Print("\r\n[WARN] RX ring buffer overflow\r\n");
  }

  while (RxRing_Get(&c))
  {
    if (c == '\r' || c == '\n')                    /* 一行結束 */
    {
      if (lineTooLong)
      {
        UART_Print("\r\n[ERR] line too long\r\n> ");
      }
      else if (lineLen > 0U)
      {
        lineBuf[lineLen] = '\0';
        UART_Print("\r\n");
        Command_Handle(lineBuf);
        UART_Print("> ");
      }
      lineLen = 0U;
      lineTooLong = 0U;
    }
    else if (c == '\b' || c == 0x7FU)              /* Backspace */
    {
      if (lineLen > 0U && !lineTooLong)
      {
        lineLen--;
        UART_Print("\b \b");
      }
    }
    else if (c >= 0x20U && c < 0x7FU)              /* 可見字元 */
    {
      if (lineLen < (LINE_BUF_SIZE - 1U))
      {
        lineBuf[lineLen++] = (char)c;
        HAL_UART_Transmit(&hlpuart1, &c, 1, 10);   /* 回顯 */
      }
      else
      {
        lineTooLong = 1U;
      }
    }
  }
}

/* ======================= 指令解析 ======================= */
static void Command_Handle(const char *cmd)
{
  if (strcmp(cmd, "banana") == 0)
  {
    App_StartMode(MODE_BANANA);
    UART_Print("[BANANA] Yellow blink + buzzer breathing, 10s. Click B1 once -> RED\r\n");
  }
  else if (strcmp(cmd, "wattermalon") == 0 || strcmp(cmd, "watermelon") == 0)
  {
    App_StartMode(MODE_WATERMELON);
    UART_Print("[WATERMELON] Green blink + buzzer breathing, 10s. Double-click B1 -> RED\r\n");
  }
  else
  {
    UART_Print("[ERR] Unknown command: ");
    UART_Print(cmd);
    UART_Print("\r\n");
  }
}

/* ======================= 狀態機 ======================= */
static void App_StartMode(AppMode_t mode)
{
  HAL_GPIO_WritePin(LED_PORT, LED_ALL_PINS, GPIO_PIN_RESET);
  __HAL_TIM_SET_COMPARE(&htim3, BUZZER_CHANNEL, 0U);
  btnClickCount   = 0U;
  g_modeStartTick = HAL_GetTick();
  g_mode          = mode;
}

static void App_EnterRed(const char *reason)
{
  if (g_mode == MODE_RED)
  {
    return;
  }
  g_mode = MODE_RED;
  HAL_GPIO_WritePin(LED_PORT, LED_ALL_PINS, GPIO_PIN_RESET);   /* 其他燈全關 */
  HAL_GPIO_WritePin(LED_PORT, LED_RED_PIN, GPIO_PIN_SET);      /* 紅燈恆亮   */
  __HAL_TIM_SET_COMPARE(&htim3, BUZZER_CHANNEL, 0U);           /* 蜂鳴器停   */

  UART_Print("\r\n[RED] ");
  UART_Print(reason);
  UART_Print("\r\n> ");
}

static void App_Task(void)
{
  uint16_t ledPin;

  if (g_mode == MODE_BANANA)
  {
    ledPin = LED_YELLOW_PIN;
  }
  else if (g_mode == MODE_WATERMELON)
  {
    ledPin = LED_GREEN_PIN;
  }
  else
  {
    return;
  }

  uint32_t elapsed = HAL_GetTick() - g_modeStartTick;

  /* 10 秒內沒觸發按鈕 → 停止並紅燈恆亮 */
  if (elapsed >= RUN_TIME_MS)
  {
    App_EnterRed("10s timeout");
    return;
  }

  /* LED 閃爍 */
  GPIO_PinState ledState = (((elapsed / BLINK_HALF_MS) & 1U) == 0U) ? GPIO_PIN_SET
                                                                      : GPIO_PIN_RESET;
  HAL_GPIO_WritePin(LED_PORT, ledPin, ledState);

  /* 呼吸燈概念：3 秒一循環，前 1.5 秒漸強、後 1.5 秒漸弱（平方曲線較平順） */
  uint32_t half  = BREATH_PERIOD_MS / 2U;
  uint32_t phase = elapsed % BREATH_PERIOD_MS;
  uint32_t level = (phase < half) ? phase : (BREATH_PERIOD_MS - phase);   /* 0 ~ half */
  uint32_t duty  = (level * level / half) * BUZZER_MAX_DUTY / half;      /* 0 ~ MAX  */
  __HAL_TIM_SET_COMPARE(&htim3, BUZZER_CHANNEL, duty);
}

/* ======================= 按鈕 ======================= */
/* 開機時取樣 16 次（約 16ms），多數決判斷閒置電位 */
static void Button_DetectIdleLevel(void)
{
  uint8_t highCount = 0U;

  for (uint8_t i = 0U; i < 16U; i++)
  {
    if (HAL_GPIO_ReadPin(BTN_PORT, BTN_PIN) == GPIO_PIN_SET)
    {
      highCount++;
    }
    HAL_Delay(1);
  }

  btnIdleLevel = (highCount >= 8U) ? GPIO_PIN_SET : GPIO_PIN_RESET;

  if (btnIdleLevel == GPIO_PIN_SET)
  {
    UART_Print("[B1] idle HIGH (pull-up): rising edge = release, click counted on release\r\n");
  }
  else
  {
    UART_Print("[B1] idle LOW: rising edge = press, click counted on press\r\n");
  }
}

/* EXTI 中斷程式：只記錄上升緣發生的時間，不在中斷裡等待 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == BTN_PIN)
  {
    btnLastEdgeTick = HAL_GetTick();     /* 每次彈跳都會刷新時間 */
    btnEdgePending  = 1U;
  }
}

/* 主迴圈：上升緣後腳位需「持續 HIGH 滿 50ms」才算一次有效按鈕
   期間讀到 LOW 代表是彈跳，直接丟棄 */
static void Button_Task(void)
{
  uint8_t  fire = 0U;
  uint32_t now;

  if (!btnEdgePending)
  {
    return;
  }

  __disable_irq();                       /* 避免和 EXTI 中斷同時改旗標 */
  now = HAL_GetTick();
  if (HAL_GPIO_ReadPin(BTN_PORT, BTN_PIN) == GPIO_PIN_RESET)
  {
    btnEdgePending = 0U;                 /* 又掉回 LOW → 彈跳 */
  }
  else if ((now - btnLastEdgeTick) >= DEBOUNCE_MS)
  {
    btnEdgePending = 0U;                 /* 穩定 HIGH 50ms → 有效 */
    fire = 1U;
  }
  __enable_irq();

  if (fire)
  {
    Button_OnClick(now);
  }
}

static void Button_OnClick(uint32_t now)
{
  switch (g_mode)
  {
    case MODE_BANANA:                    /* 按一下就紅燈 */
      App_EnterRed("B1 clicked");
      break;

    case MODE_WATERMELON:                /* 要連按兩下 */
      if (btnClickCount == 1U && (now - btnFirstClickTick) <= DOUBLE_CLICK_MS)
      {
        btnClickCount = 0U;
        App_EnterRed("B1 double-clicked");
      }
      else
      {
        btnClickCount     = 1U;          /* 第一下（或超時重新計算） */
        btnFirstClickTick = now;
        UART_Print("\r\n[B1] 1st click, click again within 500ms\r\n> ");
      }
      break;

    default:                             /* 待機 / 紅燈模式忽略按鈕 */
      btnClickCount = 0U;
      break;
  }
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
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
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
