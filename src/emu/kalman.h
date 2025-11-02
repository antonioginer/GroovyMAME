#include <cmath>

struct kalman_filter
{
    // Estado: t (posición) y P (período)
    double t = 0.0;
    double P = 0.0;

    // Covarianza (2x2)
    double C11 = 1e6, C12 = 0.0;
    double C21 = 0.0, C22 = 1e6;

    // Ruido del modelo
    double Q_t = 1e-3;  // ruido en posición
    double Q_P = 1e-6;  // ruido en período

    // Ruido de medición
    double R = 1e2;     // varianza del ruido del timestamp

    bool initialized = false;

    // Actualizar con un nuevo timestamp medido (en ns o lo que uses)
    void update(double z)
    {
        if (!initialized)
        {
            t = z;
            P = 0.0; // inicial desconocido
            initialized = true;
            return;
        }

        // --- Predicción ---
        double t_pred = t + P;
        double P_pred = P;

        // Propagación de covarianza: F = [[1,1],[0,1]]
        double C11p = C11 + 2*C12 + C22 + Q_t;
        double C12p = C12 + C22;
        double C21p = C21 + C22;
        double C22p = C22 + Q_P;

        // --- Actualización ---
        double y = z - t_pred;          // innovación
        double S = C11p + R;            // varianza de innovación
        double K1 = C11p / S;           // ganancia para t
        double K2 = C21p / S;           // ganancia para P

        // Corrección del estado
        t = t_pred + K1 * y;
        P = P_pred + K2 * y;

        // Corrección de la covarianza
        double C11n = (1 - K1) * C11p;
        double C12n = (1 - K1) * C12p;
        double C21n = C21p - K2 * C11p;
        double C22n = C22p - K2 * C12p;

        C11 = C11n;
        C12 = C12n;
        C21 = C21n;
        C22 = C22n;
    }

    // Devuelve el período estimado actual
    double get_period() const { return P; }

    // Devuelve el timestamp filtrado actual
    uint64_t get_filtered_timestamp() const { return (uint64_t)t; }
};
