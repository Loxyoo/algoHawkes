#ifndef UI_BUFFERS_H
#define UI_BUFFERS_H

#include <cstdio>
#include <cmath>
#include <string>
#include <iostream>
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"

/**
 * Buffer circulaire de points (x, y) pour un graphique défilant en temps réel.
 * Quand la capacité MaxSize est atteinte, les anciens points sont écrasés
 * via Offset (ring buffer), ce qui évite toute réallocation.
 */
struct ScrollingBuffer {
    int MaxSize;            ///< Capacité maximale du buffer
    int Offset;             ///< Indice d'écriture courant dans le ring buffer
    ImVector<ImVec2> Data;  ///< Points stockés (x = temps ImGui, y = valeur)

    ScrollingBuffer(int max_size = 2000) {
        MaxSize = max_size;
        Offset  = 0;
        Data.reserve(MaxSize);
    }

    /// Ajoute un point ; écrase le plus ancien si le buffer est plein
    void AddPoint(float x, float y) {
        if (Data.size() < MaxSize)
            Data.push_back(ImVec2(x, y));
        else {
            Data[Offset] = ImVec2(x, y);
            Offset = (Offset + 1) % MaxSize;
        }
    }

    void Erase() {
        if (Data.size() > 0) {
            Data.shrink(0);
            Offset = 0;
        }
    }
};

/**
 * Buffer glissant qui réinitialise les données quand x repasse à zéro
 * (graphique "rolling window" de durée Span secondes).
 * Contrairement à ScrollingBuffer, l'axe X est relatif à la fenêtre courante.
 */
struct RollingBuffer {
    float Span;            ///< Durée de la fenêtre glissante (secondes)
    ImVector<ImVec2> Data;

    RollingBuffer() {
        Span = 10.0f;
        Data.reserve(2000);
    }

    /// Ajoute un point ; efface tout le buffer si x a fait un tour (x < dernier x)
    void AddPoint(float x, float y) {
        float xmod = fmodf(x, Span);
        if (!Data.empty() && xmod < Data.back().x)
            Data.shrink(0);
        Data.push_back(ImVec2(xmod, y));
    }
};

#endif // UI_BUFFERS_H