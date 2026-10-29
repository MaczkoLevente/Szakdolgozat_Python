import requests
from datetime import datetime

ESP_IP = "192.168.4.1"
ESP_URL_LETOLTES = f"http://{ESP_IP}/letoltes"
ESP_URL_TORLES = f"http://{ESP_IP}/torles"
TIMEOUT_MP = 15


def adatok_letoltese_es_mentese() -> bool:
    """
    Letölti a mért CSV adatokat az ESP32-ről, elmenti a mai dátummal,
    majd törli az SD kártyán lévő adatokat, hogy tiszta lappal induljon a gyűjtés.
    """
    mai_datum = datetime.now().strftime("%Y_%m_%d")
    kimeneti_fajlnev = f"{mai_datum}.csv"

    print("\nKapcsolódás az ESP32-höz és adatok letöltése...")

    try:
        valasz = requests.get(ESP_URL_LETOLTES, timeout=TIMEOUT_MP)

        if valasz.status_code == 200:
            with open(kimeneti_fajlnev, "wb") as f:
                f.write(valasz.content)

            print(f"\n[SIKER] Az adatok letöltve és elmentve:")
            print(f" -> Fájlnév: {kimeneti_fajlnev}")


            print("-> Kérés küldése az ESP32-nek az SD kártya ürítésére...")
            torles_valasz = requests.post(ESP_URL_TORLES, timeout=5)

            if torles_valasz.status_code == 200:
                print("[SIKER] Az ESP32 kiürítette az SD kártyát, a következő mérések tiszta fájlba mennek!")
            else:
                print(f"[FIGYELEM] A törlés nem sikerült: HTTP {torles_valasz.status_code}")

            return True

        else:
            print(f"\n[HIBA] Az ESP32 válaszolt, de hibát dobott! HTTP kód: {valasz.status_code}")
            print(f"Válasz tartalma: {valasz.text}")
            return False

    except requests.exceptions.ConnectionError:
        print("\n[HIBA] Nem sikerült elérni az ESP32-t!")
        print("Ellenőrizd, hogy a laptop valóban csatlakozott-e az 'ESP32_Vilagitas' Wi-Fi hálózatra.")
        return False
    except requests.exceptions.Timeout:
        print("\n[HIBA] Időtúllépés! Az ESP32 nem fejezte be az adatküldést időben.")
        return False
    except Exception as e:
        print(f"\n[HIBA] Váratlan hiba történt: {e}")
        return False


if __name__ == "__main__":
    adatok_letoltese_es_mentese()