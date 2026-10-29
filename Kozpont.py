import os
import json
from datetime import datetime


from Megjelenito import (
    adatok_betoltese,
    grafikon_lux,
    grafikon_rb_arany,
    grafikon_teljesitmeny,
    grafikon_osszesitett,
    grafikon_pwm_es_alpha
)


from Mentes import adatok_letoltese_es_mentese
from Kuldes import fenyrecept_feltoltese


from ReceptKezelo import noveny_kivalasztasa_vagy_letrehozasa

RECEPT_FAJL = "receptek.json"


def statisztika_kiirasa(df):
    """Általános műszaki összefoglaló kiírása a konzolra."""
    ido_perc = df['Ido_sec'].iloc[-1] / 60.0
    energia = df['Energy_Wh'].iloc[-1] - df['Energy_Wh'].iloc[0] if 'Energy_Wh' in df.columns else 0.0

    print("\n" + "=" * 45)
    print("        MÉRÉSI ADATOK ÖSSZEFOGLALÓJA")
    print("=" * 45)
    print(f"Mérés időtartama:     {ido_perc:.1f} perc ({df['Ido_sec'].iloc[-1]:.0f} másodperc)")
    print(f"Érvényes minták:      {len(df)} db")
    print("-" * 45)
    print(f"Megvilágítás (Lux):   Átlag: {df['Lux'].mean():.1f} | Min: {df['Lux'].min():.1f} | Max: {df['Lux'].max():.1f}")

    valid_rb = df['RB_Arany'].dropna()
    if not valid_rb.empty:
        print(f"R/B arány (világos):  Átlag: {valid_rb.mean():.2f} | Min: {valid_rb.min():.2f} | Max: {valid_rb.max():.2f}")
    else:
        print("R/B arány:            Nincs aktív megvilágítási szakasz")

    print(f"Teljesítmény:         Átlag: {df['Power_W'].mean():.2f} W | Max: {df['Power_W'].max():.2f} W")
    print(f"Összes fogyasztás:    {energia:.3f} Wh")
    print(f"Hőmérséklet:          Átlag: {df['Temp_C'].mean():.1f} °C | Min: {df['Temp_C'].min():.1f} °C | Max: {df['Temp_C'].max():.1f} °C")
    print("=" * 45 + "\n")


def adatelemzes_almenu():
    """Adatelemző és grafikonrajzoló folyamat."""
    print("\n" + "=" * 50)
    print("    KLÍMAKAMRA ADATELEMZŐ RENDSZER")
    print("=" * 50)

    mai_fajl = f"{datetime.now().strftime('%Y_%m_%d')}.csv"
    fajl_bemenet = input(f"Add meg az elemezendő fájl nevét (Enter = '{mai_fajl}'): ").strip()
    fajlnev = fajl_bemenet if fajl_bemenet else mai_fajl

    if not fajlnev.endswith('.csv'):
        fajlnev += '.csv'

    print(f"\n-> Fájl betöltése: {fajlnev} ...")
    df = adatok_betoltese(fajlnev)

    if df is None or df.empty:
        print("[HIBA] Nem sikerült adatokat betölteni.")
        return

    statisztika_kiirasa(df)

    while True:
        print("\nMit szeretnél megjeleníteni?")
        print(" [1] Idő – Megvilágítás (Lux) grafikon")
        print(" [2] Idő – Spektrális Vörös/Kék (R/B) arány grafikon")
        print(" [3] Idő – Elektromos teljesítményfelvétel grafikon")
        print(" [4] Összesített Dashboard (minden egyben)")
        print(" [5] Beavatkozó jelek (Piros/Kék PWM és Alpha) grafikon")  # <<< ÚJ PONT
        print(" [6] Statisztikai összefoglaló újranyomtatása a konzolra")
        print(" [0] Vissza a főmenübe")

        valasztas = input("\nVálassz egy lehetőséget (0-6): ").strip()

        if valasztas == '1':
            grafikon_lux(df)
        elif valasztas == '2':
            grafikon_rb_arany(df)
        elif valasztas == '3':
            grafikon_teljesitmeny(df)
        elif valasztas == '4':
            grafikon_osszesitett(df)
        elif valasztas == '5':
            grafikon_pwm_es_alpha(df)
        elif valasztas == '6':
            statisztika_kiirasa(df)
        elif valasztas == '0':
            break
        else:
            print("[FIGYELEM] Érvénytelen választás! Kérlek 0 és 6 közötti számot adj meg.")


def fenyrecept_feltoltes_almenu():
    """Fényrecept kiválasztása, ellenőrzése és átküldése az ESP32-re."""
    print("\n" + "=" * 50)
    print("       FÉNYRECEPT FELTÖLTÉSE AZ ESP32-RE")
    print("=" * 50)

    if not os.path.exists(RECEPT_FAJL):
        print(f"[FIGYELEM] Még nincs '{RECEPT_FAJL}'. Először hozz létre egyet a 4-es menüpontban!")
        return

    try:
        with open(RECEPT_FAJL, "r", encoding="utf-8") as f:
            receptek = json.load(f)
    except Exception as e:
        print(f"[HIBA] Nem sikerült beolvasni a recepteket: {e}")
        return

    if not receptek:
        print("[FIGYELEM] A receptek adatbázisa üres. Hozz létre újat a 4-es menüpontban!")
        return

    noveny_nevek = list(receptek.keys())

    print("\nElérhető receptek:")
    for i, nev in enumerate(noveny_nevek, start=1):
        print(f" [{i}] {nev}")
    print(" [0] Vissza a főmenübe")

    valasztas = input(f"\nVálassz növényt (1-{len(noveny_nevek)}): ").strip()
    if valasztas == '0':
        return

    if not valasztas.isdigit() or not (1 <= int(valasztas) <= len(noveny_nevek)):
        print("[HIBA] Érvénytelen sorszám!")
        return

    kivalasztott_noveny = noveny_nevek[int(valasztas) - 1]
    fazisok = receptek[kivalasztott_noveny]

    print("\n" + "-" * 65)
    print(f" KIVÁLASZTOTT NÖVÉNY: {kivalasztott_noveny.upper()}")
    print("-" * 65)
    for idx, f in enumerate(fazisok, start=1):
        print(f" {idx}. Fázis: {f.get('nev', 'Névtelen')}")
        print(f"    - Időtartam:       {f.get('idotartamNap')} nap")
        print(f"    - Cél megvilágítás: {f.get('celLux')} Lux")
        print(f"    - Cél R/B arány:   {f.get('celRB')}")
        print(f"    - Nappal hossza:   {f.get('nappalHossz_s')} mp ({f.get('nappalHossz_s', 0)/3600:.1f} óra)")
        print(f"    - Éjszaka hossza:  {f.get('ejszakaHossz_s')} mp ({f.get('ejszakaHossz_s', 0)/3600:.1f} óra)")
        print(f"    - Pirkadat hossza: {f.get('pirkadatHossz_s')} mp")
        print("-" * 65)

    print("\n[FONTOS FIGYELMEZTETÉS]")
    print("A feltöltés előtt győződj meg róla, hogy a laptopoddal")
    print("csatlakoztál az ESP32 saját Wi-Fi (SoftAP) hálózatára!")

    megerosites = input("\nBiztosan elküldöd ezeket az adatokat az ESP32-nek? (i/n): ").strip().lower()

    if megerosites == 'i':
        print("\n-> Csomag küldése folyamatban...")
        siker = fenyrecept_feltoltese(kivalasztott_noveny, fazisok)
        if siker:
            print("[SIKER] A fényrecept sikeresen átadva az ESP32-nek!")
        else:
            print("[HIBA] A feltöltés megszakadt vagy sikertelen volt.")
    else:
        print("\n-> Feltöltés megszakítva. Visszatérés a főmenübe.")


def main():
    """Fő központi menü."""
    while True:
        print("\n" + "=" * 50)
        print("          KLÍMAKAMRA VEZÉRLŐ KÖZPONT")
        print("=" * 50)
        print(" [1] Mérési adatok letöltése az ESP32-ről (Mentés)")
        print(" [2] Korábbi mérési adatok elemzése (CSV / Grafikonok)")
        print(" [3] Fényrecept feltöltése az ESP32-re")
        print(" [4] Új recept létrehozása / meglévő szerkesztése")
        print(" [0] Kilépés")

        valasztas = input("\nVálassz funkciót (0-4): ").strip()

        if valasztas == '1':
            print("\n[FIGYELMEZTETÉS] Győződj meg róla, hogy csatlakoztál az ESP32 Wi-Fi-jére!")
            adatok_letoltese_es_mentese()
        elif valasztas == '2':
            adatelemzes_almenu()
        elif valasztas == '3':
            fenyrecept_feltoltes_almenu()
        elif valasztas == '4':
            noveny_kivalasztasa_vagy_letrehozasa()
        elif valasztas == '0':
            print("\nProgram leállítása. Viszlát!")
            break
        else:
            print("[FIGYELEM] Érvénytelen választás! Adj meg 0 és 4 közötti számot.")


if __name__ == "__main__":
    main()