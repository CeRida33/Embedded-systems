# MAS245 CAN-Tetris, Pi-delen.
# Filene heter fortsatt can_pingpong fordi Buildroot-pakken (can_pingpong.mk) kjører qmake på
# can_pingpong.pro og installerer binæren can_pingpong. Da trengs ingen endringer i Buildroot.
TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
CONFIG -= qt

# Modulene er header-filer, så main.cpp er eneste kildefil.
SOURCES += main.cpp
HEADERS += tetris_game.h protocol.h can_bus.h imu.h renderer.h
