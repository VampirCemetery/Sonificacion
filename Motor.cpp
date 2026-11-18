// Motor.cpp - Análisis de imagen (OpenCV) y generación de audio.
// No incluye nada de MFC ni de Windows: solo C++ estándar y OpenCV.
#include "Motor.h"
#include <fstream>
#include <iterator>
#include <numeric>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <opencv2/opencv.hpp>

// ---------------------------------------------------------------
// Constantes de audio y estado
// ---------------------------------------------------------------
static const int    SR = SR_AUDIO;       // frecuencia de muestreo (Hz)
static const double PI = 3.14159265358979323846;

static cv::Mat g_completa;               // imagen completa tal como se cargó (BGR)
static cv::Mat g_original;               // zona que se está analizando (BGR); al cargar = toda la imagen
static int     g_tono = 0;               // desplazamiento de todas las notas, en semitonos

// Lee una imagen aunque la ruta tenga acentos o caracteres especiales
static cv::Mat LeerImagenUnicode(const std::wstring& ruta) {
    std::ifstream f(ruta.c_str(), std::ios::binary);
    if (!f) return cv::Mat();
    std::vector<uchar> datos((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (datos.empty()) return cv::Mat();
    return cv::imdecode(datos, cv::IMREAD_COLOR);
}

// cv::Mat (BGR, 8 bits) -> ImagenBGR
static ImagenBGR AImagen(const cv::Mat& m) {
    cv::Mat c = m.isContinuous() ? m : m.clone();
    ImagenBGR r;
    r.ancho = c.cols;
    r.alto = c.rows;
    r.datos.assign(c.data, c.data + c.total() * c.elemSize());
    return r;
}

// ---------------------------------------------------------------
// ANÁLISIS DE IMAGEN (se usa tanto para dibujar como para el audio)
// ---------------------------------------------------------------

// Líneas: bordes (Canny) + segmentos (Hough probabilístico)
static std::vector<cv::Vec4i> DetectarLineas(cv::Mat* bordesOut = nullptr) {
    cv::Mat gris, suave, bordes;
    cv::cvtColor(g_original, gris, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gris, suave, cv::Size(5, 5), 0);
    cv::Canny(suave, bordes, 80, 160);

    std::vector<cv::Vec4i> lineas;
    int minLong = (std::max)(20, (std::min)(bordes.cols, bordes.rows) / 15);
    cv::HoughLinesP(bordes, lineas, 1, CV_PI / 180, 60, minLong, 10);
    if (bordesOut) *bordesOut = bordes;
    return lineas;
}

// Colores dominantes (k-means), ordenados del más al menos frecuente
struct ColorDom {
    cv::Vec3b bgr;
    double frac;   // fracción de la imagen (0..1)
};

static std::vector<ColorDom> AnalizarColores() {
    cv::Mat peq;
    cv::resize(g_original, peq, cv::Size(96, 96), 0, 0, cv::INTER_AREA);

    cv::Mat muestras = peq.reshape(1, peq.rows * peq.cols);
    cv::Mat muestras32;
    muestras.convertTo(muestras32, CV_32F);

    const int K = 5;
    cv::Mat etiquetas, centros;
    cv::kmeans(muestras32, K, etiquetas,
        cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::MAX_ITER, 15, 1.0),
        3, cv::KMEANS_PP_CENTERS, centros);

    std::vector<int> cuenta(K, 0);
    for (int i = 0; i < etiquetas.rows; i++) cuenta[etiquetas.at<int>(i)]++;

    std::vector<int> orden(K);
    std::iota(orden.begin(), orden.end(), 0);
    std::sort(orden.begin(), orden.end(), [&](int a, int b) { return cuenta[a] > cuenta[b]; });

    std::vector<ColorDom> res;
    double total = (double)etiquetas.rows;
    for (int k = 0; k < K; k++) {
        int idx = orden[k];
        ColorDom c;
        c.bgr = cv::Vec3b(cv::saturate_cast<uchar>(centros.at<float>(idx, 0)),
            cv::saturate_cast<uchar>(centros.at<float>(idx, 1)),
            cv::saturate_cast<uchar>(centros.at<float>(idx, 2)));
        c.frac = cuenta[idx] / total;
        res.push_back(c);
    }
    return res;
}

// ---------------------------------------------------------------
// Vistas de procesamiento (lo que se ve en el lienzo derecho)
// ---------------------------------------------------------------

// Método 1: PÍXELES
static cv::Mat CalcVistaPixeles(int x) {
    cv::Mat gris, salida;
    cv::cvtColor(g_original, gris, cv::COLOR_BGR2GRAY);
    cv::cvtColor(gris, salida, cv::COLOR_GRAY2BGR);

    x = (std::max)(0, (std::min)(x, salida.cols - 1));
    if (x + 1 < salida.cols) {
        cv::Mat resto = salida.colRange(x + 1, salida.cols);
        cv::Mat oscuro;
        resto.convertTo(oscuro, -1, 0.35, 0);
        oscuro.copyTo(resto);
    }
    int grosor = (std::max)(2, salida.cols / 300);
    cv::line(salida, cv::Point(x, 0), cv::Point(x, salida.rows - 1), cv::Scalar(0, 0, 255), grosor);
    return salida;
}

// Método 2: LÍNEAS
static cv::Mat CalcVistaLineas() {
    cv::Mat bordes, salida;
    std::vector<cv::Vec4i> lineas = DetectarLineas(&bordes);

    cv::cvtColor(bordes, salida, cv::COLOR_GRAY2BGR);
    salida.convertTo(salida, -1, 0.45, 0);
    for (const cv::Vec4i& l : lineas) {
        cv::line(salida, cv::Point(l[0], l[1]), cv::Point(l[2], l[3]),
            cv::Scalar(0, 255, 255), 2, cv::LINE_AA);
    }
    return salida;
}

// Método 3: PUNTOS
static cv::Mat CalcVistaPuntos() {
    cv::Mat gris, salida;
    cv::cvtColor(g_original, gris, cv::COLOR_BGR2GRAY);

    std::vector<cv::Point2f> puntos;
    cv::goodFeaturesToTrack(gris, puntos, 200, 0.01, 10);

    g_original.convertTo(salida, -1, 0.4, 0);
    int radio = (std::max)(3, (std::min)(salida.cols, salida.rows) / 120);
    for (const cv::Point2f& p : puntos) {
        cv::circle(salida, cv::Point(cvRound(p.x), cvRound(p.y)), radio,
            cv::Scalar(0, 255, 0), -1, cv::LINE_AA);
    }
    return salida;
}

// Método 4: COLORES
static cv::Mat CalcVistaColores() {
    std::vector<ColorDom> colores = AnalizarColores();
    int K = (int)colores.size();

    cv::Mat salida(g_original.size(), CV_8UC3, cv::Scalar(20, 20, 20));
    double escala = (std::max)(0.5, salida.rows / 600.0);
    int x0 = 0;
    for (int k = 0; k < K; k++) {
        int ancho = (k == K - 1) ? (salida.cols - x0)
            : cvRound(colores[k].frac * salida.cols);
        if (ancho > 0) {
            cv::Scalar color(colores[k].bgr[0], colores[k].bgr[1], colores[k].bgr[2]);
            cv::rectangle(salida, cv::Rect(x0, 0, ancho, salida.rows), color, cv::FILLED);

            if (ancho > 45 * escala) {
                std::string txt = std::to_string(cvRound(100.0 * colores[k].frac)) + "%";
                cv::Point pos(x0 + 8, salida.rows - 14);
                cv::putText(salida, txt, pos, cv::FONT_HERSHEY_SIMPLEX, escala,
                    cv::Scalar(0, 0, 0), 4, cv::LINE_AA);
                cv::putText(salida, txt, pos, cv::FONT_HERSHEY_SIMPLEX, escala,
                    cv::Scalar(255, 255, 255), 1, cv::LINE_AA);
            }
        }
        x0 += ancho;
    }
    return salida;
}

// ---------------------------------------------------------------
// AUDIO: síntesis básica
// ---------------------------------------------------------------

// Frecuencia (Hz) de una nota de la escala pentatónica mayor.
// indice 0 = Do3, 1 = Re3, 2 = Mi3, 3 = Sol3, 4 = La3, 5 = Do4, ...
// Se usa pentatónica para que las notas suenen bien juntas.
static double FrecPentatonica(int indice, int midiBase = 48) {
    static const int grados[5] = { 0, 2, 4, 7, 9 };
    if (indice < 0) indice = 0;
    int midi = midiBase + 12 * (indice / 5) + grados[indice % 5];
    return 440.0 * std::pow(2.0, (midi - 69 + g_tono) / 12.0);
}

// Suma una nota (onda senoidal + armónicos) al buffer, empezando en 'inicio' segundos.
//  brillo      0..1 : cuánto armónico tiene (timbre más brillante)
//  decaimiento      : qué tan rápido se apaga (0 = sostenida, 3 = tipo pluck)
//  ataqueSeg / relajoSeg : cuánto tarda en entrar y en salir (más largo = más suave)
static void AgregarNota(std::vector<float>& buf, double inicio, double dur,
    double freq, double amp, double brillo, double decaimiento,
    double ataqueSeg = 0.01, double relajoSeg = 0.05) {
    size_t n0 = (size_t)(inicio * SR);
    size_t n = (size_t)(dur * SR);
    if (n < 2) return;
    if (buf.size() < n0 + n) buf.resize(n0 + n, 0.0f);

    size_t ataque = (std::min)((size_t)(ataqueSeg * SR), n / 2);
    size_t relajo = (std::min)((size_t)(relajoSeg * SR), n / 2);

    for (size_t i = 0; i < n; i++) {
        double t = (double)i / SR;
        double env = std::exp(-decaimiento * (double)i / (double)n);
        if (i < ataque) env *= (double)i / (double)ataque;
        if (i + relajo > n) env *= (double)(n - i) / (double)relajo;

        double fase = 2.0 * PI * freq * t;
        double s = std::sin(fase)
            + brillo * 0.5 * std::sin(2.0 * fase)
            + brillo * 0.25 * std::sin(3.0 * fase);
        buf[n0 + i] += (float)(amp * env * s);
    }
}

// Ajusta el volumen máximo a 'pico' y aplica un pequeño fade-in / fade-out
static void Normalizar(std::vector<float>& s, float pico = 0.9f) {
    float m = 0.0f;
    for (float v : s) m = (std::max)(m, std::fabs(v));
    if (m > 1e-6f) {
        float g = pico / m;
        for (float& v : s) v *= g;
    }
    size_t f = (std::min)(s.size() / 2, (size_t)(0.02 * SR));
    for (size_t i = 0; i < f; i++) {
        float k = (float)i / (float)f;
        s[i] *= k;
        s[s.size() - 1 - i] *= k;
    }
}

// Construye un archivo WAV (PCM 16 bits, mono) a partir de las muestras
static void ConstruirWav(const std::vector<float>& s, std::vector<uint8_t>& out) {
    uint32_t dataSize = (uint32_t)(s.size() * 2);
    out.assign(44 + dataSize, 0);

    auto w32 = [&](size_t off, uint32_t v) { memcpy(&out[off], &v, 4); };
    auto w16 = [&](size_t off, uint16_t v) { memcpy(&out[off], &v, 2); };

    memcpy(&out[0], "RIFF", 4);
    w32(4, 36 + dataSize);
    memcpy(&out[8], "WAVE", 4);
    memcpy(&out[12], "fmt ", 4);
    w32(16, 16);           // tamaño del bloque fmt
    w16(20, 1);            // PCM
    w16(22, 1);            // mono
    w32(24, SR);           // frecuencia de muestreo
    w32(28, SR * 2);       // bytes por segundo
    w16(32, 2);            // bytes por muestra
    w16(34, 16);           // bits por muestra
    memcpy(&out[36], "data", 4);
    w32(40, dataSize);

    for (size_t i = 0; i < s.size(); i++) {
        float v = (std::max)(-1.0f, (std::min)(1.0f, s[i]));
        int16_t m = (int16_t)std::lround(v * 32767.0f);
        memcpy(&out[44 + i * 2], &m, 2);
    }
}

// ---------------------------------------------------------------
// AUDIO: un generador por método
// ---------------------------------------------------------------

// PÍXELES: la imagen funciona como un espectrograma (cada columna = un instante).
//   La imagen se divide en 24 franjas horizontales; cada franja es una nota
//   (arriba = agudo, abajo = grave, escala pentatónica).
//   El brillo de la franja en esa columna = volumen de esa nota.
//   Así, zonas claras y oscuras distintas producen acordes distintos que van cambiando.
//   Duración fija (DUR_PIXELES): una columna = un instante.
static std::vector<float> AudioPixeles() {
    cv::Mat gris;
    cv::cvtColor(g_original, gris, cv::COLOR_BGR2GRAY);
    int cols = gris.cols;

    const int BANDAS = 24;
    cv::Mat franjas;   // BANDAS filas x cols columnas: brillo medio de cada franja (0..1)
    cv::resize(gris, franjas, cv::Size(cols, BANDAS), 0, 0, cv::INTER_AREA);
    franjas.convertTo(franjas, CV_32F, 1.0 / 255.0);
    cv::blur(franjas, franjas, cv::Size(15, 1));   // suaviza en el tiempo
    cv::pow(franjas, 2.0, franjas);                // resalta lo claro frente a lo oscuro

    double freq[BANDAS];
    double fase[BANDAS];
    for (int b = 0; b < BANDAS; b++) { freq[b] = FrecPentatonica(b, 48); fase[b] = 0.0; }

    size_t N = (size_t)(DUR_PIXELES * SR);
    std::vector<float> out(N);
    for (size_t i = 0; i < N; i++) {
        double pos = (double)i / (double)(N - 1) * (cols - 1);
        int c0 = (int)pos;
        int c1 = (std::min)(c0 + 1, cols - 1);
        double fr = pos - c0;

        double s = 0.0;
        for (int r = 0; r < BANDAS; r++) {
            double amp = (double)franjas.at<float>(r, c0) * (1.0 - fr)
                + (double)franjas.at<float>(r, c1) * fr;
            int b = BANDAS - 1 - r;                 // fila de arriba = nota más aguda
            fase[b] += 2.0 * PI * freq[b] / SR;     // fase continua = sin clics
            s += amp * std::sin(fase[b]);
        }
        out[i] = (float)s;
    }
    return out;
}

// LÍNEAS: la imagen se divide en 32 "tiempos" de izquierda a derecha.
//   En cada tiempo suena UNA nota: la de la línea más larga de esa zona.
//   orientación (0-180°) -> nota; longitud -> duración y volumen.
//   Si en una zona no hay líneas, hay silencio (así se forma un ritmo).
static std::vector<float> AudioLineas() {
    std::vector<cv::Vec4i> lineas = DetectarLineas();
    const int PASOS = 32;
    const double PASO_SEG = 0.4;
    std::vector<float> buf((size_t)((PASOS * PASO_SEG + 1.0) * SR), 0.0f);
    if (lineas.empty()) return buf;

    struct L { double x, ang, len; };
    std::vector<L> v;
    double maxLen = 1.0;
    for (const cv::Vec4i& l : lineas) {
        double dx = l[2] - l[0], dy = l[3] - l[1];
        L e;
        e.len = std::sqrt(dx * dx + dy * dy);
        e.ang = std::atan2(dy, dx) * 180.0 / PI;
        if (e.ang < 0) e.ang += 180.0;
        if (e.ang >= 180.0) e.ang -= 180.0;
        e.x = (l[0] + l[2]) / 2.0;
        v.push_back(e);
        maxLen = (std::max)(maxLen, e.len);
    }

    int cols = g_original.cols;
    std::vector<int> mejor(PASOS, -1);   // índice de la línea más larga de cada tiempo
    for (size_t k = 0; k < v.size(); k++) {
        int p = (std::max)(0, (std::min)(PASOS - 1, (int)(v[k].x / cols * PASOS)));
        if (mejor[p] < 0 || v[k].len > v[mejor[p]].len) mejor[p] = (int)k;
    }

    for (int p = 0; p < PASOS; p++) {
        if (mejor[p] < 0) continue;
        const L& e = v[mejor[p]];
        double dur = 0.4 + 0.5 * e.len / maxLen;
        int idx = (std::min)(14, (int)(e.ang / 180.0 * 15.0));
        double amp = 0.3 + 0.7 * e.len / maxLen;
        AgregarNota(buf, p * PASO_SEG, dur, FrecPentatonica(idx), amp, 0.3, 1.5, 0.02, 0.25);
    }
    return buf;
}

// PUNTOS: la imagen se divide en 40 "tiempos" de izquierda a derecha.
//   En cada tiempo suena UNA nota corta: la del punto más fuerte de esa zona.
//   y (arriba -> abajo) -> nota (arriba agudo, abajo grave); brillo del píxel -> volumen.
static std::vector<float> AudioPuntos() {
    cv::Mat gris;
    cv::cvtColor(g_original, gris, cv::COLOR_BGR2GRAY);
    std::vector<cv::Point2f> puntos;
    cv::goodFeaturesToTrack(gris, puntos, 200, 0.01, 10);   // vienen ordenados del más fuerte al más débil

    const int PASOS = 40;
    const double PASO_SEG = 0.3;
    std::vector<float> buf((size_t)((PASOS * PASO_SEG + 1.0) * SR), 0.0f);

    std::vector<int> mejor(PASOS, -1);
    for (size_t k = 0; k < puntos.size(); k++) {
        int p = (std::max)(0, (std::min)(PASOS - 1, (int)(puntos[k].x / gris.cols * PASOS)));
        if (mejor[p] < 0) mejor[p] = (int)k;   // el primero es el más fuerte
    }

    for (int p = 0; p < PASOS; p++) {
        if (mejor[p] < 0) continue;
        const cv::Point2f& pt = puntos[mejor[p]];
        int px = (std::max)(0, (std::min)(gris.cols - 1, cvRound(pt.x)));
        int py = (std::max)(0, (std::min)(gris.rows - 1, cvRound(pt.y)));
        int idx = (int)((1.0 - (double)py / gris.rows) * 20.0);
        idx = (std::max)(0, (std::min)(19, idx));
        double amp = 0.3 + 0.7 * gris.at<uchar>(py, px) / 255.0;
        AgregarNota(buf, p * PASO_SEG, 0.45, FrecPentatonica(idx), amp, 0.4, 4.0, 0.005, 0.1);
    }
    return buf;
}

// COLORES: los 5 colores dominantes suenan uno tras otro, con transiciones suaves
// (cada nota entra mientras la anterior se apaga) y al final todos juntos (acorde).
//   tono (hue) -> nota; valor (brillo) -> volumen y octava; saturación -> timbre;
//   porcentaje -> duración de su nota.
static std::vector<float> AudioColores() {
    std::vector<ColorDom> colores = AnalizarColores();
    std::vector<float> buf;
    double t = 0.0;
    const double SOLAPE = 0.6;   // segundos que se empalman dos notas seguidas

    struct Nota { double freq, amp, brillo; };
    std::vector<Nota> notas;

    for (const ColorDom& c : colores) {
        cv::Mat px(1, 1, CV_8UC3, cv::Scalar(c.bgr[0], c.bgr[1], c.bgr[2]));
        cv::Mat hsv;
        cv::cvtColor(px, hsv, cv::COLOR_BGR2HSV);
        cv::Vec3b h = hsv.at<cv::Vec3b>(0, 0);   // H: 0..179, S: 0..255, V: 0..255

        int idx = (int)(h[0] / 180.0 * 10.0);
        if (h[2] >= 128) idx += 5;
        idx = (std::min)(14, idx);

        Nota n;
        n.freq = FrecPentatonica(idx);
        n.amp = 0.25 + 0.75 * h[2] / 255.0;
        n.brillo = h[1] / 255.0;
        notas.push_back(n);

        double dur = (std::max)(1.2, 6.0 * c.frac + 0.8);
        AgregarNota(buf, t, dur, n.freq, n.amp, n.brillo, 0.3, 0.5, 0.8);
        t += dur - SOLAPE;
    }
    // Acorde final: entra poco a poco y se apaga despacio
    for (const Nota& n : notas)
        AgregarNota(buf, t, 4.0, n.freq, n.amp * 0.4, n.brillo, 0.3, 1.2, 1.5);
    return buf;
}


// Genera el audio del método elegido y lo devuelve como WAV
static std::vector<uint8_t> GenerarWavInterno(int metodo) {
    std::vector<float> s;
    switch (metodo) {
    case 1: s = AudioPixeles(); break;
    case 2: s = AudioLineas();  break;
    case 3: s = AudioPuntos();  break;
    case 4: s = AudioColores(); break;
    }
    if (s.empty()) s.assign(SR, 0.0f);

    Normalizar(s);
    std::vector<uint8_t> wav;
    ConstruirWav(s, wav);
    return wav;
}

// ---------------------------------------------------------------
// Funciones públicas (las que usa la ventana MFC)
// ---------------------------------------------------------------
namespace Motor {

    bool CargarImagen(const std::wstring& ruta) {
        cv::Mat img = LeerImagenUnicode(ruta);
        if (img.empty()) return false;

        // Si la imagen es muy grande se reduce para que el análisis sea rápido
        const int MAX_LADO = 1200;
        int lado = (std::max)(img.cols, img.rows);
        if (lado > MAX_LADO) {
            double r = (double)MAX_LADO / lado;
            cv::resize(img, img, cv::Size(), r, r, cv::INTER_AREA);
        }
        g_completa = img;
        g_original = img;
        return true;
    }

    ImagenBGR ImagenCompleta() { return g_completa.empty() ? ImagenBGR() : AImagen(g_completa); }

    bool EstablecerRegion(int x, int y, int ancho, int alto) {
        if (g_completa.empty()) return false;
        x = (std::max)(0, (std::min)(x, g_completa.cols - 1));
        y = (std::max)(0, (std::min)(y, g_completa.rows - 1));
        ancho = (std::min)(ancho, g_completa.cols - x);
        alto = (std::min)(alto, g_completa.rows - y);

        const int MIN_LADO = 32;   // una zona más pequeña no da suficiente información para analizar
        if (ancho < MIN_LADO || alto < MIN_LADO) return false;

        g_original = g_completa(cv::Rect(x, y, ancho, alto)).clone();
        return true;
    }

    void SetTono(int semitonos) { g_tono = (std::max)(-12, (std::min)(12, semitonos)); }

    ImagenBGR ImagenOriginal() { return g_original.empty() ? ImagenBGR() : AImagen(g_original); }

    ImagenBGR VistaPixeles(int columna) { return g_original.empty() ? ImagenBGR() : AImagen(CalcVistaPixeles(columna)); }
    ImagenBGR VistaLineas() { return g_original.empty() ? ImagenBGR() : AImagen(CalcVistaLineas()); }
    ImagenBGR VistaPuntos() { return g_original.empty() ? ImagenBGR() : AImagen(CalcVistaPuntos()); }
    ImagenBGR VistaColores() { return g_original.empty() ? ImagenBGR() : AImagen(CalcVistaColores()); }

    ImagenBGR EscalarProporcional(const ImagenBGR& src, int maxAncho, int maxAlto) {
        if (src.vacia() || maxAncho <= 0 || maxAlto <= 0) return ImagenBGR();
        cv::Mat origen(src.alto, src.ancho, CV_8UC3, const_cast<uint8_t*>(src.datos.data()));
        double ratio = (std::min)((double)maxAncho / src.ancho, (double)maxAlto / src.alto);
        cv::Mat dst;
        cv::resize(origen, dst, cv::Size(), ratio, ratio, cv::INTER_AREA);
        return AImagen(dst);
    }

    std::vector<uint8_t> GenerarWav(int metodo) {
        if (g_original.empty()) return std::vector<uint8_t>();
        return GenerarWavInterno(metodo);
    }

}  // namespace Motor