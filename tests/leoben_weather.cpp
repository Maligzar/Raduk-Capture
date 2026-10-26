// ============================================================
//  Weather from Leoben, and the home-network door
//
//  Leoben is another machine and another project, so nothing about it
//  can be assumed on the bench -- but everything RadukCapture does with
//  what it sends can be checked here:
//
//    1. The reply is read correctly, in either JSON spacing, and a
//       reply that is not a reading is refused rather than half-used.
//    2. Sea-level pressure becomes STATION pressure. The ADC needs the
//       air above the telescope; the sea-level figure overstates it by
//       ~6% at this site, which is exactly the error Phase 66 removed.
//    3. The ADC uses live air only while it is fresh, and refuses
//       nonsense outright.
//    4. LeobenUrl is understood, and a malformed one is refused.
//    5. The home-network exemption admits private addresses only --
//       never loopback, never a public address.
//    6. The weather fields survive a round trip through the
//       observation record.
// ============================================================

#include "net/LeobenWeather.h"
#include "net/WsProtocol.h"
#include "core/ADCAssistant.h"
#include "core/ObservationRecord.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace raduk;

static int g_fail = 0;
static void Check(bool ok, const std::string& what, const std::string& detail = "") {
    printf("  [%s] %-62s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (!ok) g_fail++;
}
static std::string F(double v, int d = 2) { char b[48]; snprintf(b, sizeof(b), "%.*f", d, v); return b; }

// A /weather/current body in the shape main.py's handler returns: the
// snapshot, a trend list, and the summary. Compact, as Starlette emits.
static const char* kCompact =
    R"({"available":true,"snapshot":{"id":"x","captured_at":"2026-10-25T03:00:00+00:00",)"
    R"("source":"ensemble","cloud_pct":12.0,"temp_f":59.0,"humidity_pct":40.0,"wind_mph":6.0,)"
    R"("gust_mph":11.0,"dew_point_f":35.0,"pressure_inhg":30.05,"visibility_mi":10.0,)"
    R"("imaging_score":82,"imaging_label":"EXCELLENT","wind_stability_score":70,)"
    R"("dew_risk":"safe","wind_gust_spike":false,"forecast_confidence":"high",)"
    R"("exceeds_critical":false,"created_at":"x"},"trends":[{"metric":"max_cloud_pct","delta":3}],)"
    R"("summary":{"score":82,"label":"EXCELLENT","wind_mss":70,"dew_risk":"safe","go_nogo":"GO"}})";

int main() {
    printf("\n=== Leoben weather, and the home-network door ===\n");

    printf("\n1. Reading the reply\n");
    {
        net::LeobenConditions c;
        Check(net::ParseLeobenCurrent(kCompact, c), "a real-shaped reply parses");
        Check(c.cloudPct == 12.0 && c.tempF == 59.0 && c.windMph == 6.0 && c.gustMph == 11.0,
              "  cloud, temperature, wind and gust");
        Check(c.pressureMslInHg == 30.05 && c.dewPointF == 35.0 && c.humidityPct == 40.0,
              "  pressure, dew point, humidity");
        Check(c.imagingScore == 82 && c.goNoGo == "GO" && c.dewRisk == "safe" &&
              c.imagingLabel == "EXCELLENT", "  score, verdict, dew risk, label");
        Check(std::abs(c.TempC() - 15.0) < 1e-9 && std::abs(c.DewPointC() - 1.6667) < 1e-3,
              "  Fahrenheit to Celsius");
        Check(std::abs(c.WindMs() - 2.68224) < 1e-6, "  mph to m/s", F(c.WindMs(), 5));

        std::string spaced = kCompact;
        size_t p = 0;
        while ((p = spaced.find("\":", p)) != std::string::npos) { spaced.replace(p, 2, "\" : "); p += 4; }
        net::LeobenConditions s;
        Check(net::ParseLeobenCurrent(spaced, s) && s.cloudPct == 12.0 && s.goNoGo == "GO",
              "spaces around every colon change nothing");

        net::LeobenConditions n;
        Check(!net::ParseLeobenCurrent(R"({"available":false,"error":"Weather data unavailable"})", n) && !n.valid,
              "\"available\": false is not a reading");
        std::string zeroP = kCompact; zeroP.replace(zeroP.find("30.05"), 5, "0.0");
        Check(!net::ParseLeobenCurrent(zeroP, n), "a pressure of 0 (Leoben's default) is refused");
        std::string noCloud = kCompact; noCloud.replace(noCloud.find("\"cloud_pct\":12.0"), 16, "\"cloud_pct\":null");
        Check(!net::ParseLeobenCurrent(noCloud, n), "a null in a field the app uses is refused");
        Check(!net::ParseLeobenCurrent("<html>502 Bad Gateway</html>", n), "an error page is refused");

        double v = 0;
        Check(!net::JsonNumber(R"({"max_cloud_pct":99})", "cloud_pct", v),
              "\"cloud_pct\" is not found inside \"max_cloud_pct\"");
        std::string str;
        Check(!net::JsonString(R"({"go_nogo":"G\"O"})", "go_nogo", str),
              "a string with an escape is refused, not half-read");
    }

    printf("\n2. Sea level to station pressure\n");
    {
        Check(net::StationPressureHpa(1013.25, 0.0, 15.0) == 1013.25, "at 0 m it is unchanged");
        const double st = net::StationPressureHpa(1013.25, 528.0, 15.0);
        // The standard atmosphere at 528 m, the ADC model's own figure.
        const double model = 1013.25 * std::pow(1.0 - 2.25577e-5 * 528.0, 5.25588);
        Check(std::abs(st - model) < 1.0, "528 m at 15 C agrees with the standard atmosphere",
              F(st, 1) + " vs " + F(model, 1) + " hPa");
        Check(st < 1013.25 * 0.95, "  and is ~6% below sea level, the trap this avoids",
              F((1.0 - st / 1013.25) * 100.0, 1) + "% lower");
        Check(net::StationPressureHpa(1013.25, 528.0, -10.0) < st,
              "colder air thins faster with height");
        net::LeobenConditions c; net::ParseLeobenCurrent(kCompact, c);
        Check(std::abs(c.PressureMslHpa() - 1017.61) < 0.05, "inHg to hPa", F(c.PressureMslHpa(), 2));
    }

    printf("\n3. The ADC and live air\n");
    {
        using core::ADCAssistant; using core::ADCAir;
        ADCAir model = ADCAssistant::ModelAir();
        const auto base = ADCAssistant::Compute(30.0, model);
        Check(!base.airLive && base.airTempK == 288.0f, "the model air is marked as the model");
        ADCAir dense = model; dense.pressureHpa = model.pressureHpa * 1.05; dense.tempK = 268.0; dense.live = true;
        const auto live = ADCAssistant::Compute(30.0, dense);
        const double expect = base.separationArcsec * 1.05 * (288.0 / 268.0);
        Check(live.airLive && std::abs(live.separationArcsec - expect) < 0.01,
              "dispersion scales with P/T", F(base.separationArcsec) + "\" -> " + F(live.separationArcsec) + "\"");

        ADCAssistant::ClearLiveAir();
        const int64_t now = 1'800'000'000;
        ADCAssistant::SetLiveAir(960.0, 270.0, now - 60);
        Check(ADCAssistant::CurrentAir(now).live && ADCAssistant::CurrentAir(now).pressureHpa == 960.0,
              "a minute-old reading is used");
        Check(!ADCAssistant::CurrentAir(now + 31 * 60).live,
              "past 30 minutes the standard atmosphere is back");
        Check(!ADCAssistant::CurrentAir(now - 3600).live,
              "a reading from the future (clock change) is not trusted");
        ADCAssistant::ClearLiveAir();
        ADCAssistant::SetLiveAir(101325.0, 270.0, now);     // pascals by mistake
        Check(!ADCAssistant::CurrentAir(now).live, "a pressure in the wrong unit is refused");
        ADCAssistant::SetLiveAir(960.0, 15.0, now);         // Celsius by mistake
        Check(!ADCAssistant::CurrentAir(now).live, "a temperature in the wrong unit is refused");
        ADCAssistant::ClearLiveAir();
    }

    printf("\n4. LeobenUrl\n");
    {
        net::LeobenEndpoint e;
        Check(net::ParseLeobenUrl("http://elminster:5000", e) && e.host == L"elminster" && e.port == 5000,
              "http://elminster:5000");
        Check(net::ParseLeobenUrl("http://192.168.1.17:5000/", e) && e.host == L"192.168.1.17",
              "an address, with a trailing slash");
        Check(net::ParseLeobenUrl("http://leoben", e) && e.port == 80, "no port means 80");
        Check(!net::ParseLeobenUrl("https://elminster:5000", e), "https is refused (Leoben serves http)");
        Check(!net::ParseLeobenUrl("elminster:5000", e), "a missing scheme is refused");
        Check(!net::ParseLeobenUrl("http://elminster:5000/weather", e), "a path is refused");
        Check(!net::ParseLeobenUrl("http://user@elminster:5000", e), "a user is refused");
        Check(!net::ParseLeobenUrl("http://elminster:99999", e), "a port out of range is refused");
        Check(!net::ParseLeobenUrl("", e), "empty is off, not an error");
    }

    printf("\n5. Who needs no token on the home network\n");
    {
        using net::ws::IsPrivateLanPeer; using net::ws::PeerExemptFromToken;
        Check(IsPrivateLanPeer(0xC0A80111), "192.168.1.17");
        Check(IsPrivateLanPeer(0x0A000005), "10.0.0.5");
        Check(IsPrivateLanPeer(0xAC100001) && IsPrivateLanPeer(0xAC1FFFFE), "172.16.0.1 and 172.31.255.254");
        Check(!IsPrivateLanPeer(0xAC0F0001) && !IsPrivateLanPeer(0xAC200001), "172.15.x and 172.32.x are public");
        Check(IsPrivateLanPeer(0xA9FE0102), "169.254.1.2, link-local");
        Check(!IsPrivateLanPeer(0x7F000001), "127.0.0.1 is NOT exempt -- the capture PC's own browser");
        Check(!IsPrivateLanPeer(0x08080808), "8.8.8.8 is not exempt");
        Check(!IsPrivateLanPeer(0), "an unknown address is not exempt");
        Check(!PeerExemptFromToken(false, 0xC0A80111), "with WsLanOpen off, nobody is exempt");
        Check(PeerExemptFromToken(true, 0xC0A80111), "with it on, a home address is");
    }

    printf("\n6. The observation record keeps the sky\n");
    {
        core::ObservationRecord r;
        r.file = "x.ser"; r.wxSource = "leoben"; r.wxAgeMin = 4.0; r.cloudPct = 12;
        r.airTempC = 15.0; r.humidityPct = 40; r.windMs = 2.7; r.gustMs = 4.9;
        r.dewPointC = 1.7; r.pressureHpa = 957.3; r.imagingScore = 82; r.wxVerdict = "GO";
        const std::string text = core::SerializeObservation(r);
        core::ObservationRecord back; std::string why;
        const bool ok = core::ParseObservation(text, back, &why);
        Check(ok && back.wxSource == "leoben" && back.wxVerdict == "GO", "source and verdict", why);
        Check(back.cloudPct == 12 && back.airTempC == 15.0 && back.pressureHpa == 957.3 &&
              back.imagingScore == 82 && back.windMs == 2.7, "the readings, to their written precision");
        core::ObservationRecord none; none.file = "y.ser";
        Check(core::SerializeObservation(none).find("cloud_pct") == std::string::npos,
              "with no reading, no weather key is written at all");
    }

    printf("\n%s\n", g_fail == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return g_fail == 0 ? 0 : 1;
}
