
POSSIBLE SENSORS THAT CAN BE USED FOR INTENTION DETECTION AND MOTION/PERSON PRESENCE DETECTION
    ---PIR sensor
        --cone like structure
        --can customize sensitivity and distance of detection by available potentialmeter
        --susceptible to noise like other heat source eg. the sun
    ---RADAR (micro&mm wave radiation, work by emitting radiation.. (don't like metal.., ))
        --microWave RADAR (~3GHz)
            -can't customize sensitivity and delay timing(manually in code..)
            -doesn't trip easily on interference
            -detect ..people when moving...
        --mmWave RADAR (~24GHz)
            -do not just detect people when moving but also micro movement like heartbeat regardless whether a person is physically stationary

    ---Ultrasonic Sensor and LASER sensor
        --ultrasonic sensor
            -shorter length
            -detect either stationary or moving objetive
            -not effective by light interference as compared to laser sensor, affected by sound... (hence not suitable for noisy envir..)
        --LASER sensor (sending light pulses)
            -more accurate than ultrasonic sensor
            -longer range
            -susceptible to interference from light in the infra-red spectrum region
            -not affected by sound hence good choice in noise environment





