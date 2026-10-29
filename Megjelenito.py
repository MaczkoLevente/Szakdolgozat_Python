import os
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

OSZLOPOK = [
    'Ido_ms', 'Temp_C', 'Hum_pct', 'Volt_V', 'Current_mA',
    'Energy_Wh', 'Lux', 'Voros', 'Zold', 'Kek', 'Tiszta',
    'PWM_Piros_pct', 'PWM_Kek_pct', 'Alpha'
]


def adatok_betoltese(fajlnev):
    if not os.path.exists(fajlnev):
        print(f"[HIBA] A megadott fájl nem található: {fajlnev}")
        return None

    df = pd.read_csv(fajlnev, sep=';', header=None, names=OSZLOPOK, low_memory=False)
    df = df[pd.to_numeric(df['Ido_ms'], errors='coerce').notnull()].copy()
    for col in OSZLOPOK:
        df[col] = pd.to_numeric(df[col])

    # Újraindítások szűrése
    ujraindulasok = df[df['Ido_ms'].diff() < 0].index
    if not ujraindulasok.empty:
        df = df.iloc[ujraindulasok[-1]:].copy()
    else:
        df = df.copy()

    df['Ido_sec'] = (df['Ido_ms'] - df['Ido_ms'].iloc[0]) / 1000.0
    df['Power_W'] = df['Volt_V'] * (df['Current_mA'] / 1000.0)

    # R/B arány és sötét szakaszok szűrése
    kek_tisztitott = df['Kek'].replace(0, np.nan)
    df['RB_Arany'] = df['Voros'] / kek_tisztitott
    df.loc[df['Lux'] < 10, 'RB_Arany'] = np.nan

    return df


def grafikon_lux(df):
    plt.figure(figsize=(9, 5))
    plt.plot(df['Ido_sec'], df['Lux'], color='darkorange', label='Mért Lux', linewidth=1.5)

    # Statisztika szövegdobozban
    szoveg = f"Min: {df['Lux'].min():.1f} Lux\nÁtlag: {df['Lux'].mean():.1f} Lux\nMax: {df['Lux'].max():.1f} Lux"
    plt.gca().text(0.02, 0.95, szoveg, transform=plt.gca().transAxes, verticalalignment='top',
                   bbox=dict(boxstyle='round', facecolor='white', alpha=0.8))

    plt.title("Megvilágítás alakulása")
    plt.xlabel("Eltelt idő [s]")
    plt.ylabel("Megvilágítás [Lux]")
    plt.grid(True, linestyle='--', alpha=0.6)
    plt.legend(loc='upper right')
    plt.tight_layout()
    plt.show()


def grafikon_rb_arany(df):
    plt.figure(figsize=(9, 5))
    plt.plot(df['Ido_sec'], df['RB_Arany'], color='crimson', label='R/B Arány', linewidth=1.5)

    valid_rb = df['RB_Arany'].dropna()
    if not valid_rb.empty:
        szoveg = f"Min: {valid_rb.min():.2f}\nÁtlag: {valid_rb.mean():.2f}\nMax: {valid_rb.max():.2f}"
    else:
        szoveg = "Nincs aktív fázis"

    plt.gca().text(0.02, 0.95, szoveg, transform=plt.gca().transAxes, verticalalignment='top',
                   bbox=dict(boxstyle='round', facecolor='white', alpha=0.8))

    plt.title("Vörös / Kék arány alakulása")
    plt.xlabel("Eltelt idő [s]")
    plt.ylabel("R / B arány [-]")
    plt.grid(True, linestyle='--', alpha=0.6)
    plt.legend(loc='upper right')
    plt.tight_layout()
    plt.show()


def grafikon_teljesitmeny(df):
    plt.figure(figsize=(9, 5))
    plt.plot(df['Ido_sec'], df['Power_W'], color='navy', label='Pillanatnyi teljesítmény', linewidth=1.5)

    energia_felvett = df['Energy_Wh'].iloc[-1] - df['Energy_Wh'].iloc[0] if 'Energy_Wh' in df.columns else 0.0
    szoveg = (f"Min: {df['Power_W'].min():.2f} W\n"
              f"Átlag: {df['Power_W'].mean():.2f} W\n"
              f"Max: {df['Power_W'].max():.2f} W\n"
              f"Fogyasztás: {energia_felvett:.3f} Wh")

    plt.gca().text(0.02, 0.95, szoveg, transform=plt.gca().transAxes, verticalalignment='top',
                   bbox=dict(boxstyle='round', facecolor='white', alpha=0.8))

    plt.title("Világítás elektromos teljesítménye")
    plt.xlabel("Eltelt idő [s]")
    plt.ylabel("Teljesítmény [W]")
    plt.grid(True, linestyle='--', alpha=0.6)
    plt.legend(loc='upper right')
    plt.tight_layout()
    plt.show()


def grafikon_osszesitett(df):
    fig, ax = plt.subplots(4, 1, figsize=(11, 11), sharex=True)

    # 1. Lux
    ax[0].plot(df['Ido_sec'], df['Lux'], color='darkorange', label='Lux')
    ax[0].set_ylabel("Lux")
    ax[0].grid(True, linestyle='--', alpha=0.6)
    stat_lux = f"Átlag: {df['Lux'].mean():.1f} | Max: {df['Lux'].max():.1f}"
    ax[0].set_title(f"Klímakamra mérések  [{stat_lux}]")

    # 2. R/B arány
    ax[1].plot(df['Ido_sec'], df['RB_Arany'], color='crimson', label='R/B arány')
    ax[1].set_ylabel("R / B")
    ax[1].grid(True, linestyle='--', alpha=0.6)
    valid_rb = df['RB_Arany'].dropna()
    if not valid_rb.empty:
        ax[1].text(0.02, 0.85, f"Átlag: {valid_rb.mean():.2f}", transform=ax[1].transAxes,
                   bbox=dict(boxstyle='square', facecolor='white', alpha=0.7))

    # 3. Teljesítmény
    ax[2].plot(df['Ido_sec'], df['Power_W'], color='navy', label='Teljesítmény [W]')
    ax[2].set_ylabel("Watt")
    ax[2].grid(True, linestyle='--', alpha=0.6)
    energia = df['Energy_Wh'].iloc[-1] - df['Energy_Wh'].iloc[0] if 'Energy_Wh' in df.columns else 0.0
    ax[2].text(0.02, 0.85, f"Átlag: {df['Power_W'].mean():.2f} W | Összes: {energia:.2f} Wh",
               transform=ax[2].transAxes, bbox=dict(boxstyle='square', facecolor='white', alpha=0.7))

    # 4. Hőmérséklet
    ax[3].plot(df['Ido_sec'], df['Temp_C'], color='forestgreen', label='Hőmérséklet [°C]')
    ax[3].set_ylabel("°C")
    ax[3].set_xlabel("Eltelt idő [s]")
    ax[3].grid(True, linestyle='--', alpha=0.6)
    ax[3].text(0.02, 0.85,
               f"Átlag: {df['Temp_C'].mean():.1f} °C (Min: {df['Temp_C'].min():.1f}, Max: {df['Temp_C'].max():.1f})",
               transform=ax[3].transAxes, bbox=dict(boxstyle='square', facecolor='white', alpha=0.7))

    plt.tight_layout()
    plt.show()

def grafikon_pwm_es_alpha(df):
    """
    Közös grafikon a beavatkozó jelekről:
    - Bal tengely: Piros PWM [%] és Kék PWM [%]
    - Jobb tengely: Alpha [-] szürke színnel
    """
    import matplotlib.pyplot as plt

    # Ha a fájlban még nem szerepelnek ezek az oszlopok, vagy csupa NaN
    if 'PWM_Piros_pct' not in df.columns or df['PWM_Piros_pct'].dropna().empty:
        print("[FIGYELEM] Ebben a mérési fájlban még nem szerepelnek PWM és Alpha adatok!")
        return


    ido_perc = df['Ido_sec'] / 60.0

    fig, ax1 = plt.subplots(figsize=(10, 6))

    # Bal tengely: PWM kitöltések (%)
    ax1.set_xlabel('Idő [perc]', fontsize=11)
    ax1.set_ylabel('PWM kitöltés [%]', color='black', fontsize=11)
    vonal_piros = ax1.plot(ido_perc, df['PWM_Piros_pct'], color='red', label='Piros PWM [%]', linewidth=1.5)
    vonal_kek   = ax1.plot(ido_perc, df['PWM_Kek_pct'],   color='blue', label='Kék PWM [%]', linewidth=1.5)
    ax1.set_ylim(-5, 105)
    ax1.grid(True, linestyle='--', alpha=0.6)

    # Jobb tengely: Alpha érték (0-1 arány)
    ax2 = ax1.twinx()
    ax2.set_ylabel('Alpha spektrális súly [-]', color='gray', fontsize=11)
    vonal_alpha = ax2.plot(ido_perc, df['Alpha'], color='gray', linestyle='--', label='Alpha [-]', linewidth=1.8)
    ax2.set_ylim(0.0, 1.0)
    ax2.tick_params(axis='y', labelcolor='gray')

    # Közös jelmagyarázat (Legend) összeállítása
    vonalak = vonal_piros + vonal_kek + vonal_alpha
    cimkek = [v.get_label() for v in vonalak]
    ax1.legend(vonalak, cimkek, loc='upper left')

    plt.title('Beavatkozó jelek dinamikája (PWM & Alpha)', fontsize=13, fontweight='bold')
    plt.tight_layout()
    plt.show()

if __name__ == "__main__":
    kivalasztott_fajl = input("Melyik fájl-t elemezzük? (pl. 2026_09_23.csv): ").strip()
    if not kivalasztott_fajl.endswith('.csv'):
        kivalasztott_fajl += '.csv'

    meresi_adatok = adatok_betoltese(kivalasztott_fajl)

    if meresi_adatok is not None:
        print("\n--- Válassz megjelenítést ---")
        print("1: Összesített dashboard")
        print("2: Csak Lux")
        print("3: Csak R/B arány")
        print("4: Csak Elektromos teljesítmény")
        valasztas = input("Opció (1-4, Enter = 1): ").strip()

        if valasztas == "2":
            grafikon_lux(meresi_adatok)
        elif valasztas == "3":
            grafikon_rb_arany(meresi_adatok)
        elif valasztas == "4":
            grafikon_teljesitmeny(meresi_adatok)
        else:
            grafikon_osszesitett(meresi_adatok)
