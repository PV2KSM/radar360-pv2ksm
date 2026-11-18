#pragma once
// Mapa de hardware do GeekMagic Radar 360 (placa "R V2.0", modelo radar21en).
// Extraido do firmware original 0.4.41 (board_init e display_lvgl_init).

// Tela redonda 360x360, controlador Galaxycore (familia GC9B71/GC9B72), SPI 1 bit
#define PIN_LCD_SCLK   12
#define PIN_LCD_MOSI   11
#define PIN_LCD_DC     13
#define PIN_LCD_RST     8
#define PIN_LCD_CS     -1   // CS fixo no flat
#define PIN_LCD_TE     15   // Tearing effect (entrada)
#define LCD_SPI_HZ     40000000   // 80 MHz e o unico passo acima (opcao "Tela turbo"); 60 MHz virava 40 de qualquer jeito
#define LCD_W          360
#define LCD_H          360

// Backlight: PWM 20 kHz, invertido (transistor PNP: nivel baixo = aceso)
#define PIN_BACKLIGHT   5
#define BL_PWM_HZ      20000

// Botao unico (ativo em nivel baixo, pull-up interno)
#define PIN_BUTTON     38

// Buzzer (componente nao montado na placa; manter desligado)
#define PIN_BUZZER      9
