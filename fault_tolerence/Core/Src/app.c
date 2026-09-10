#include "app.h"
#include "app_config.h"
#include "semphr.h"
#include <stdarg.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

static AppSnapshot_t state;
static SemaphoreHandle_t state_lock, uart_lock;
static UART_HandleTypeDef *uart;
static TaskHandle_t tasks[APP_TASK_COUNT];
#define IWDG_WAIT_LOOPS 10000000UL

static void BootLog(const char *s) { if (uart != NULL) (void)HAL_UART_Transmit(uart, (uint8_t *)s, (uint16_t)strlen(s), 100U); }
static BaseType_t WaitSet(volatile uint32_t *r, uint32_t m) { uint32_t n=IWDG_WAIT_LOOPS; while(((*r&m)==0U)&&(n--!=0U)){} return((*r&m)!=0U)?pdPASS:pdFAIL; }
static BaseType_t WaitClear(volatile uint32_t *r, uint32_t m) { uint32_t n=IWDG_WAIT_LOOPS; while(((*r&m)!=0U)&&(n--!=0U)){} return((*r&m)==0U)?pdPASS:pdFAIL; }
static void Lock(void){(void)xSemaphoreTake(state_lock,portMAX_DELAY);} static void Unlock(void){(void)xSemaphoreGive(state_lock);}
static void Beat(AppTaskId_t i){Lock();state.heartbeat[i].tick=xTaskGetTickCount();state.heartbeat[i].count++;Unlock();}
static void Snapshot(AppSnapshot_t *s){Lock();*s=state;Unlock();}
static void SetState(AppSystemState_t s){Lock();state.state=s;Unlock();}
static void Fault(AppFaultType_t f,AppTaskId_t t,uint32_t e){Lock();state.fault.type=f;state.fault.task=t;state.fault.i2c_error=e;state.fault.count++;Unlock();}
static void SensorTask(void *a); static void ValidationTask(void *a); static void SupervisorTask(void *a);

void App_Log(const char *fmt,...){char b[192];va_list a;int n;if(uart==NULL)return;va_start(a,fmt);n=vsnprintf(b,sizeof(b)-3U,fmt,a);va_end(a);if(n<0)return;if(n>(int)(sizeof(b)-3U))n=(int)(sizeof(b)-3U);b[n++]='\r';b[n++]='\n';if((uart_lock!=NULL)&&(xSemaphoreTake(uart_lock,pdMS_TO_TICKS(100U))==pdTRUE)){(void)HAL_UART_Transmit(uart,(uint8_t*)b,(uint16_t)n,200U);(void)xSemaphoreGive(uart_lock);}}
int __io_putchar(int c){uint8_t b=(uint8_t)c;if(uart!=NULL)(void)HAL_UART_Transmit(uart,&b,1U,100U);return c;} int __io_getchar(void){return -1;}

static BaseType_t WatchdogInit(void){BootLog("[IWDG] LSI enable\r\n");RCC->CSR|=RCC_CSR_LSION;if(WaitSet(&RCC->CSR,RCC_CSR_LSIRDY)!=pdPASS){BootLog("[IWDG] LSI NOT READY\r\n");return pdFAIL;}IWDG1->KR=0xCCCCU;IWDG1->KR=0x5555U;IWDG1->PR=APP_IWDG_PRESCALER_BITS;if(WaitClear(&IWDG1->SR,IWDG_SR_PVU)!=pdPASS)return pdFAIL;IWDG1->RLR=APP_IWDG_RELOAD;if(WaitClear(&IWDG1->SR,IWDG_SR_RVU)!=pdPASS)return pdFAIL;IWDG1->KR=0xAAAAU;BootLog("[IWDG] Initialized\r\n");return pdPASS;}
static void WatchdogRefresh(void){if(xTaskGetCurrentTaskHandle()==tasks[APP_TASK_SUPERVISOR])IWDG1->KR=0xAAAAU;}

void App_Initialize(I2C_HandleTypeDef *i2c,UART_HandleTypeDef *u){state_lock=xSemaphoreCreateMutex();uart_lock=xSemaphoreCreateMutex();uart=u;state.state=APP_SYSTEM_STARTING;state.validation_status=APP_VALIDATION_PENDING;state.reset_flags=RCC->RSR;MPU6050_Bind(i2c);}
BaseType_t App_CreateTasks(void){BootLog("[RTOS] Before Sensor task\r\n");if(xTaskCreate(SensorTask,"MPU6050",384U,NULL,3U,&tasks[APP_TASK_SENSOR])!=pdPASS)return pdFAIL;BootLog("[RTOS] Sensor task created\r\n");if(xTaskCreate(ValidationTask,"Validate",384U,NULL,2U,&tasks[APP_TASK_VALIDATION])!=pdPASS)return pdFAIL;BootLog("[RTOS] Validate task created\r\n");if(xTaskCreate(SupervisorTask,"Supervisor",512U,NULL,4U,&tasks[APP_TASK_SUPERVISOR])!=pdPASS)return pdFAIL;BootLog("[RTOS] Supervisor task created\r\n[IWDG] Initializing\r\n");return WatchdogInit();}

static void SensorTask(void *a){MPU6050_Status_t status;MPU6050_Measurement_t m;uint32_t fails=0U;(void)a;App_Log("[TASK] Sensor started");status=MPU6050_Initialize();if(status==MPU6050_STATUS_OK){App_Log("[IMU] MPU6050 detected WHO_AM_I=0x68");App_Log("[IMU] Initialized");}else App_Log("[IMU] WHO_AM_I/I2C initialization failed");for(;;){
#if APP_FAULT_INJECTION_MODE == APP_FAULT_INJECTION_SENSOR_STALL
if((HAL_GetTick()>=APP_FAULT_INJECTION_START_MS)&&(HAL_GetTick()<(APP_FAULT_INJECTION_START_MS+3000U)))vTaskDelay(pdMS_TO_TICKS(3500U));
#endif
Beat(APP_TASK_SENSOR);status=MPU6050_Read(&m);
#if APP_FAULT_INJECTION_MODE == APP_FAULT_INJECTION_I2C_ERROR
if((HAL_GetTick()>=APP_FAULT_INJECTION_START_MS)&&(HAL_GetTick()<(APP_FAULT_INJECTION_START_MS+1500U)))status=MPU6050_STATUS_I2C_ERROR;
#endif
Lock();if(status==MPU6050_STATUS_OK){state.measurement=m;state.sensor_status=APP_SENSOR_OK;state.consecutive_i2c_failures=0U;fails=0U;}else{state.sensor_status=(status==MPU6050_STATUS_IDENTITY_ERROR)?APP_SENSOR_IDENTITY_ERROR:APP_SENSOR_I2C_ERROR;state.consecutive_i2c_failures=++fails;}Unlock();if(status!=MPU6050_STATUS_OK)Fault((status==MPU6050_STATUS_IDENTITY_ERROR)?APP_FAULT_SENSOR_IDENTITY:APP_FAULT_I2C,APP_TASK_SENSOR,MPU6050_LastI2cError());if(fails>=APP_I2C_FAILURE_LIMIT){App_Log("[IMU] Recovery attempt");(void)MPU6050_Recover();fails=0U;}vTaskDelay(pdMS_TO_TICKS(APP_SENSOR_PERIOD_MS));}}

static void ValidationTask(void *a){AppSnapshot_t s;AppValidationStatus_t v;float g;(void)a;App_Log("[TASK] Validate started");for(;;){Beat(APP_TASK_VALIDATION);Snapshot(&s);v=APP_VALIDATION_OK;if((s.measurement.timestamp_ms==0U)||(HAL_GetTick()-s.measurement.timestamp_ms>APP_MEASUREMENT_TIMEOUT_MS))v=APP_VALIDATION_STALE;else if(!isfinite(s.measurement.accel_x_g)||!isfinite(s.measurement.accel_y_g)||!isfinite(s.measurement.accel_z_g)||!isfinite(s.measurement.gyro_x_dps)||!isfinite(s.measurement.gyro_y_dps)||!isfinite(s.measurement.gyro_z_dps)||fabsf(s.measurement.accel_x_g)>2.2f||fabsf(s.measurement.accel_y_g)>2.2f||fabsf(s.measurement.accel_z_g)>2.2f||fabsf(s.measurement.gyro_x_dps)>275.0f||fabsf(s.measurement.gyro_y_dps)>275.0f||fabsf(s.measurement.gyro_z_dps)>275.0f)v=APP_VALIDATION_INVALID;else{g=sqrtf(s.measurement.accel_x_g*s.measurement.accel_x_g+s.measurement.accel_y_g*s.measurement.accel_y_g+s.measurement.accel_z_g*s.measurement.accel_z_g);if((g<0.20f)||(g>3.20f))v=APP_VALIDATION_INVALID;}Lock();state.validation_status=v;Unlock();if(v==APP_VALIDATION_STALE)Fault(APP_FAULT_STALE_MEASUREMENT,APP_TASK_VALIDATION,0U);else if(v==APP_VALIDATION_INVALID)Fault(APP_FAULT_INVALID_MEASUREMENT,APP_TASK_VALIDATION,0U);vTaskDelay(pdMS_TO_TICKS(APP_VALIDATION_PERIOD_MS));}}

static BaseType_t Restart(AppTaskId_t id){TaskFunction_t e=(id==APP_TASK_SENSOR)?SensorTask:ValidationTask;if(tasks[id]!=NULL)vTaskDelete(tasks[id]);return xTaskCreate(e,(id==APP_TASK_SENSOR)?"MPU6050":"Validate",384U,NULL,(id==APP_TASK_SENSOR)?3U:2U,&tasks[id]);}
static const char *StateName(AppSystemState_t s){return(s==APP_SYSTEM_HEALTHY)?"HEALTHY":(s==APP_SYSTEM_RECOVERING)?"RECOVERING":"SAFE";}
static const char *FaultName(AppFaultType_t f){switch(f){case APP_FAULT_SENSOR_HEARTBEAT:return "SENSOR HEARTBEAT TIMEOUT";case APP_FAULT_VALIDATION_HEARTBEAT:return "VALIDATION HEARTBEAT TIMEOUT";case APP_FAULT_I2C:return "MPU6050 I2C FAILURE";case APP_FAULT_SENSOR_IDENTITY:return "MPU6050 WHO_AM_I FAILED";case APP_FAULT_STALE_MEASUREMENT:return "STALE IMU MEASUREMENT";case APP_FAULT_INVALID_MEASUREMENT:return "INVALID IMU MEASUREMENT";default:return "UNKNOWN";}}
static void Print(AppSnapshot_t *s){App_Log("IMU A: X=%ldmg Y=%ldmg Z=%ldmg",(long)(s->measurement.accel_x_g*1000.0f),(long)(s->measurement.accel_y_g*1000.0f),(long)(s->measurement.accel_z_g*1000.0f));App_Log("IMU G: X=%ld Y=%ld Z=%ld dps",(long)s->measurement.gyro_x_dps,(long)s->measurement.gyro_y_dps,(long)s->measurement.gyro_z_dps);App_Log("Tasks S:%s V:%s W:OK",s->sensor_status==APP_SENSOR_OK?"OK":"FAULT",s->validation_status==APP_VALIDATION_OK?"OK":"FAULT");App_Log("SYSTEM STATUS : %s",StateName(s->state));}
static void SupervisorTask(void *a){AppSnapshot_t s;TickType_t last=0U;uint32_t tries=0U;(void)a;App_Log("[TASK] Supervisor started");App_Log(" FAULT-TOLERANT MPU6050 RTOS SYSTEM");for(;;){
#if APP_FAULT_INJECTION_MODE == APP_FAULT_INJECTION_SUPERVISOR_STALL
if((HAL_GetTick()>=APP_FAULT_INJECTION_START_MS)&&(HAL_GetTick()<(APP_FAULT_INJECTION_START_MS+15000U))){vTaskDelay(pdMS_TO_TICKS(1000U));continue;}
#endif
Beat(APP_TASK_SUPERVISOR);Snapshot(&s);bool sl=(xTaskGetTickCount()-s.heartbeat[APP_TASK_SENSOR].tick)>pdMS_TO_TICKS(APP_HEARTBEAT_TIMEOUT_MS);bool vl=(xTaskGetTickCount()-s.heartbeat[APP_TASK_VALIDATION].tick)>pdMS_TO_TICKS(APP_HEARTBEAT_TIMEOUT_MS);if(!sl&&!vl&&s.sensor_status==APP_SENSOR_OK&&s.validation_status==APP_VALIDATION_OK){tries=0U;SetState(APP_SYSTEM_HEALTHY);WatchdogRefresh();}else if(sl||vl){SetState(APP_SYSTEM_RECOVERING);if(tries++<APP_RECOVERY_LIMIT){Fault(sl?APP_FAULT_SENSOR_HEARTBEAT:APP_FAULT_VALIDATION_HEARTBEAT,sl?APP_TASK_SENSOR:APP_TASK_VALIDATION,0U);(void)Restart(sl?APP_TASK_SENSOR:APP_TASK_VALIDATION);}if(tries>=APP_RECOVERY_LIMIT)SetState(APP_SYSTEM_SAFE);}else{AppFaultType_t f=(s.sensor_status==APP_SENSOR_IDENTITY_ERROR)?APP_FAULT_SENSOR_IDENTITY:(s.sensor_status==APP_SENSOR_I2C_ERROR)?APP_FAULT_I2C:(s.validation_status==APP_VALIDATION_STALE)?APP_FAULT_STALE_MEASUREMENT:APP_FAULT_INVALID_MEASUREMENT;if(s.state!=APP_SYSTEM_SAFE){Fault(f,APP_TASK_SENSOR,MPU6050_LastI2cError());App_Log("*** FAULT DETECTED *** %s",FaultName(f));}SetState(APP_SYSTEM_SAFE);tries=0U;WatchdogRefresh();}Snapshot(&s);if((xTaskGetTickCount()-last)>=pdMS_TO_TICKS(2000U)){Print(&s);last=xTaskGetTickCount();}vTaskDelay(pdMS_TO_TICKS(APP_SUPERVISOR_PERIOD_MS));}}
