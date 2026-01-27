## Project Description

This repository contains source codes developed as part of my engineering thesis project entitled:

**Model of Self-Balancing Multi-Rotor Beam**

The project consists of two main software components:

### Arduino Control Code
- core control logic of the self-balancing multi-rotor beam system
- implementation of control algorithms and system behavior
- acquisition and processing of IMU sensor data
- actuator and PWM signal control
- this code represents the main and essential part of the entire project

### Python Communication and Logging Tool
- serves as a custom terminal for communication with the microcontroller
- enables sending control commands to the system
- allows receiving real-time data from the IMU (current orientation and measurement values)
- logs measurement data to a dedicated output file
- recorded data is used for plotting graphs, system analysis, and formulation of conclusions in the thesis

## Repository Contents
The software components described above are implemented in the following files and directories:

- `Self-Balancing_Beam/`  
  Contains the Arduino control code described in the *Arduino Control Code* section.

- `Logger.py`  
  Implements the Python communication and logging tool described in the *Python Communication and Logging Tool* section.

## Author:
- Martyna Rydczak
