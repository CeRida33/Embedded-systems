TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += main.cpp
HEADERS += tetris_game.h protocol.h can_bus.h imu.h renderer.h
