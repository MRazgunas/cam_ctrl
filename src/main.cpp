#include <ch.h>
#include <hal.h>
#include <math.h>

#include <uavcan/uavcan.hpp>
#include <uavcan_stm32/uavcan_stm32.hpp>
#include <kmti/camera/PictureTaken.hpp>
#include <kmti/camera/TriggerCommand.hpp>

/*
 * standard 9600 baud serial config.
 */
static const SerialConfig serialCfg = {
  9600,
  0,
  0,
  0
};


static THD_WORKING_AREA(waThread1, 128);
void Thread1(void) {
  chRegSetThreadName("blinker");

  while(1) {
    palClearPad(GPIOB, GPIOB_LED);
    chThdSleepMilliseconds(500);
    palSetPad(GPIOB, GPIOB_LED);
    chThdSleepMilliseconds(500);
  }
}

constexpr unsigned NodePoolSize = 400;

uavcan_stm32::CanInitHelper<> can;

uavcan::Node<NodePoolSize>& getNode() {
  static uavcan::Node<NodePoolSize> node(can.driver, uavcan_stm32::SystemClock::instance());
  return node;
}

//2 transmit start
const uint8_t trigger_code[] = { 2, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1 };

bool trigg_transm = false; //Are we currently transmitting
uint8_t code_pos = 0; //Current transmition bit
uint8_t pulse_pos = 0; //0 - high 1, 2, 3 - low
uint16_t pulse_counter = 0;

//0 transmit 24 pulse (96 triggers) + 96 triggers gap
//1 transmit 48 pulse (192 triggers) + 96 triggers gap
//2 start code 384 trigger + 96 triggers gap

void IR_led_control(GPTDriver *gptp) {
    (void) gptp;

    if(trigg_transm) {
        if(trigger_code[code_pos] == 2) {
            if(pulse_counter < 384) {
                if(pulse_counter % 4 == 0) {
                    palSetPad(GPIOC, GPIOC_CAM_POWER_BUT);
                } else {
                    palClearPad(GPIOC, GPIOC_CAM_POWER_BUT);
                }
                pulse_counter++;
            } else if(pulse_counter == 479) {
                code_pos++;
                pulse_counter = 0;
            } else pulse_counter++;
        }
        else if(trigger_code[code_pos] == 1){
            //Transmit 1
            if(pulse_counter < 192) {
                if(pulse_counter % 4 == 0) {
                    palSetPad(GPIOC, GPIOC_CAM_POWER_BUT);
                } else {
                    palClearPad(GPIOC, GPIOC_CAM_POWER_BUT);
                }
                pulse_counter++;
            } else if(pulse_counter == 287) {
                code_pos++;
                pulse_counter = 0;
            } else pulse_counter++;
        } else {
            //Transmit 0
            if(pulse_counter < 96) {
                if(pulse_counter % 4 == 0) {
                    palSetPad(GPIOC, GPIOC_CAM_POWER_BUT);
                } else {
                    palClearPad(GPIOC, GPIOC_CAM_POWER_BUT);
                }
                pulse_counter++;
            } else if (pulse_counter == 192) {
                code_pos++;
                pulse_counter = 0;
            } else pulse_counter++;
        }
        if(code_pos == sizeof(trigger_code)) {
            trigg_transm = false;
            code_pos = 0;
        }
    }
}

static const GPTConfig gptcfg = {
  4*160000,              /* 160kHz timer clock.*/
  IR_led_control,    /* Timer callback.*/
  0,
  0
};

void triggerCamera(float delay) {
    (void) delay;
    //TODO: support delay
    if(trigg_transm == false) trigg_transm = true;
}

int main(void) {
  halInit();
  chSysInit();
  sdStart(&SD1, &serialCfg);

  chThdCreateStatic(waThread1, sizeof(waThread1), NORMALPRIO, (tfunc_t)Thread1, NULL);


  uavcan::uint32_t bitrate = 1000000;
  can.init(bitrate);

  getNode().setName("org.kmti.camera");
  getNode().setNodeID(10);

  if (getNode().start() < 0) {
    chSysHalt("UAVCAN init fail");
  }

  uavcan::Publisher<kmti::camera::PictureTaken> pic_pub(getNode());
  pic_pub.init();

  getNode().setModeOperational();

  uavcan::Subscriber<kmti::camera::TriggerCommand> trigg_sub(getNode());

  const int trigg_sub_start_res = trigg_sub.start(
          [&](const uavcan::ReceivedDataStructure<kmti::camera::TriggerCommand>& msg)
          {
              triggerCamera(msg.delay);
          });

  if(trigg_sub_start_res < 0) {
      //chSysHalt("Failed to start trigger subscriber");
  }

  gptStart(&GPTD3, &gptcfg);
  gptStartContinuous(&GPTD3, 3);
  while(1) {
      //getNode().spin(uavcan::MonotonicDuration::fromMSec(1000));
      chThdSleepMilliseconds(1000);
      triggerCamera(0.0f);
  }
}

