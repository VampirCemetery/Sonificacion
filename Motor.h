// Motor.h - Parte "motor" del programa: análisis de imagen con OpenCV y generación de audio.
// No usa MFC ni Windows: así el código de OpenCV y el de la ventana (MFC) quedan separados.
#pragma once
#include <string>
#include <vector>
#include <cstdint>

// Segundos que dura el barrido del método de píxeles (la ventana lo usa para sincronizar la línea roja)
constexpr double DUR_PIXELES = 10.0;

// Frecuencia de muestreo del audio (Hz). La ventana la usa para calcular tiempos dentro del WAV.
constexpr int SR_AUDIO = 44100;

// Imagen simple: 3 bytes por píxel, en orden B, G, R (como la guarda OpenCV)
struct ImagenBGR {
    int ancho = 0;
    int alto = 0;
    std::vector<uint8_t> datos;   // ancho * alto * 3 bytes
    bool vacia() const { return datos.empty(); }
};

namespace Motor {
    // Lee la imagen del disco (acepta rutas con acentos). Si es muy grande, la reduce a 1200 px.
    bool CargarImagen(const std::wstring& ruta);

    // ImagenCompleta  = la imagen entera tal como se cargó.
    // ImagenOriginal  = la zona que se está analizando (al cargar es toda la imagen;
    //                   después de EstablecerRegion es solo la zona elegida).
    ImagenBGR ImagenCompleta();
    ImagenBGR ImagenOriginal();

    // Elige la zona de la imagen (en píxeles de la imagen completa) que se va a analizar.
    // Devuelve false si no hay imagen o si la zona es demasiado pequeña (menos de 32 px por lado).
    bool EstablecerRegion(int x, int y, int ancho, int alto);

    // Sube o baja todas las notas (-12 a +12 semitonos). Se aplica al generar el siguiente audio.
    void SetTono(int semitonos);

    // Lo que se muestra en el lienzo derecho para cada método
    ImagenBGR VistaPixeles(int columna);
    ImagenBGR VistaLineas();
    ImagenBGR VistaPuntos();
    ImagenBGR VistaColores();

    // Reduce o amplía la imagen para que quepa en (maxAncho x maxAlto) sin deformarla
    ImagenBGR EscalarProporcional(const ImagenBGR& src, int maxAncho, int maxAlto);

    // Genera el audio del método elegido (1 = píxeles, 2 = líneas, 3 = puntos, 4 = colores)
    // y lo devuelve como archivo WAV completo (cabecera + muestras) en memoria.
    std::vector<uint8_t> GenerarWav(int metodo);
}