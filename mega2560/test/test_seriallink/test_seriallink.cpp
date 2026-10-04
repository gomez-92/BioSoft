#include <ArduinoFake.h>
#include <unity.h>
#include <string>

#include "../support/arduino_fakes.hpp"
#include "../../src/seriallink.hpp"
#include "../../src/commands.hpp"

using namespace fakeit;

// =====================================================================
// SerialLink de punta a punta: una trama que sale de un SerialLink entra al
// parser de otro y llega igual.
//
// La cola de salida guarda bytes de tramas ya armadas (no JsonDocuments):
// sendCommand() escribe el JSON a mano. Si ese armado se equivocara en un
// byte, el CRC lo descartaria del otro lado EN SILENCIO -- el comando no
// llegaria nunca y nada diria por que. Por eso se prueba con el parser real.
// =====================================================================

namespace {

    // Un Stream en memoria: lo que se escribe queda en `written`; lo que se
    // lee sale de `input`.
    class MemoryStream : public Stream {
    public:
        std::string written;
        std::string input;
        size_t readPos = 0;

        size_t write(uint8_t b) override { written.push_back((char)b); return 1; }
        size_t write(const uint8_t* buffer, size_t size) override {
            written.append((const char*)buffer, size);
            return size;
        }
        int available() override { return (int)(input.size() - readPos); }
        int read() override { return readPos < input.size() ? (uint8_t)input[readPos++] : -1; }
        int peek() override { return readPos < input.size() ? (uint8_t)input[readPos] : -1; }
        void flush() override {}
    };

    class Recorder : public CommandListener {
    public:
        int count = 0;
        std::string command;
        std::string params;
        void onSerialConnected() override {}
        void onSerialDisconnected() override {}
        void onCommand(const char* cmd, JsonVariantConst p) override {
            count++;
            command = cmd;
            params.clear();
            serializeJson(p, params);
        }
    };

    unsigned long fakeNow = 0;

    // Avanza el reloj de a 60 ms (> TX_INTERVAL_MS) y corre update() hasta
    // que la cola se vacie: cada update() saca una trama.
    void drain(SerialLink& link, int rounds = 40) {
        for (int i = 0; i < rounds; i++) {
            fakeNow += 60;
            link.update();
        }
    }

    // Pasa lo que escribio `from` al parser de `to` y lo procesa.
    void deliver(MemoryStream& from, MemoryStream& toStream, SerialLink& to) {
        toStream.input = from.written;
        toStream.readPos = 0;
        from.written.clear();
        fakeNow += 1;
        to.update();
    }
}

void setUp(void) {
    ArduinoFakeReset();
    silenceSerial();
    fakeNow = 1000;
    When(Method(ArduinoFake(), millis)).AlwaysDo([]() -> unsigned long { return fakeNow; });
    When(OverloadedMethod(ArduinoFake(), random, long(long, long))).AlwaysReturn(42);
}

void tearDown(void) {}

// Los bytes del JSON son exactamente los de serializar el sobre
// {command, params}: el receptor (y el firmware viejo del otro lado) no
// tiene que notar el cambio.
void test_frame_carries_the_same_json_as_an_envelope(void) {
    MemoryStream out;
    SerialLink tx(out);
    tx.begin();

    JsonDocument params;
    params["reason"] = "refused";
    params["description"] = "Campo nulo sin punto en el mapa";
    params["cause"] = "nomap";
    TEST_ASSERT_TRUE(tx.sendCommand(Commands::ResultData, params));
    drain(tx);

    JsonDocument envelope;
    envelope["command"] = Commands::ResultData;
    envelope["params"] = params;
    std::string expected;
    serializeJson(envelope, expected);

    const std::string& frame = out.written;
    TEST_ASSERT_EQUAL_UINT32(expected.size() + 5, frame.size());
    TEST_ASSERT_EQUAL_UINT8(SERIAL_STX, (uint8_t)frame[0]);
    TEST_ASSERT_EQUAL_UINT32(expected.size(), (uint8_t)frame[1] | ((uint8_t)frame[2] << 8));
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), frame.substr(3, expected.size()).c_str());
    TEST_ASSERT_EQUAL_UINT8(SERIAL_ETX, (uint8_t)frame.back());
}

// La trama armada pasa el CRC y el parser del otro lado: llega el comando
// con sus params intactos.
void test_frame_survives_the_real_parser(void) {
    MemoryStream out, in;
    SerialLink tx(out), rx(in);
    Recorder recorder;
    tx.begin();
    rx.begin();
    rx.setListener(&recorder);

    JsonDocument params;
    params["reason"] = "critical";
    params["source"] = "CEM1";
    params["type"] = "frequency";
    params["count"] = 3;
    params["limit"] = 3;
    TEST_ASSERT_TRUE(tx.sendCommand(Commands::ResultData, params));
    drain(tx);
    deliver(out, in, rx);

    std::string expected;
    serializeJson(params, expected);
    TEST_ASSERT_EQUAL_INT(1, recorder.count);
    TEST_ASSERT_EQUAL_STRING(Commands::ResultData, recorder.command.c_str());
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), recorder.params.c_str());
}

// Varias tramas encoladas salen en orden y de a una por intervalo.
void test_frames_leave_in_order(void) {
    MemoryStream out, in;
    SerialLink tx(out), rx(in);
    Recorder recorder;
    tx.begin();
    rx.begin();
    rx.setListener(&recorder);

    for (int i = 0; i < 3; i++) {
        JsonDocument params;
        params["value"] = i;
        TEST_ASSERT_TRUE(tx.sendCommand(Commands::Ping, params));
    }
    // Un solo intervalo: sale una sola trama.
    fakeNow += 60;
    tx.update();
    deliver(out, in, rx);
    TEST_ASSERT_EQUAL_INT(1, recorder.count);
    TEST_ASSERT_EQUAL_STRING("{\"value\":0}", recorder.params.c_str());

    drain(tx);
    deliver(out, in, rx);
    TEST_ASSERT_EQUAL_INT(3, recorder.count);
    TEST_ASSERT_EQUAL_STRING("{\"value\":2}", recorder.params.c_str());
}

// Un documento vacio viaja como "params":null, igual que antes.
void test_empty_params_travel_as_null(void) {
    MemoryStream out, in;
    SerialLink tx(out), rx(in);
    Recorder recorder;
    tx.begin();
    rx.begin();
    rx.setListener(&recorder);

    JsonDocument params;
    TEST_ASSERT_TRUE(tx.sendCommand(Commands::Reset, params));
    drain(tx);
    deliver(out, in, rx);

    TEST_ASSERT_EQUAL_INT(1, recorder.count);
    TEST_ASSERT_EQUAL_STRING(Commands::Reset, recorder.command.c_str());
    TEST_ASSERT_EQUAL_STRING("null", recorder.params.c_str());
}

// Una trama que no entra en MAX_JSON_SIZE no sale. Antes se truncaba y
// llegaba con CRC valido y menos claves.
void test_oversize_frame_is_refused_not_truncated(void) {
    MemoryStream out;
    SerialLink tx(out);
    tx.begin();

    JsonDocument params;
    params["d"] = std::string(MAX_JSON_SIZE, 'x').c_str();
    TEST_ASSERT_FALSE(tx.sendCommand(Commands::ConfigRule, params));
    drain(tx);
    TEST_ASSERT_EQUAL_UINT32(0, out.written.size());
}

// Con la cola llena sendCommand devuelve false (los que reintentan lo
// vuelven a mandar); al vaciarse vuelve a aceptar.
void test_full_buffer_refuses_until_it_drains(void) {
    MemoryStream out;
    SerialLink tx(out);
    tx.begin();

    JsonDocument params;
    params["d"] = std::string(200, 'x').c_str();
    int accepted = 0;
    while (tx.sendCommand(Commands::ConfigRule, params)) accepted++;
    TEST_ASSERT_TRUE(accepted >= 1);
    TEST_ASSERT_TRUE(accepted * 230 <= TX_BUFFER_SIZE);

    drain(tx);
    TEST_ASSERT_TRUE(tx.sendCommand(Commands::ConfigRule, params));
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_frame_carries_the_same_json_as_an_envelope);
    RUN_TEST(test_frame_survives_the_real_parser);
    RUN_TEST(test_frames_leave_in_order);
    RUN_TEST(test_empty_params_travel_as_null);
    RUN_TEST(test_oversize_frame_is_refused_not_truncated);
    RUN_TEST(test_full_buffer_refuses_until_it_drains);
    return UNITY_END();
}
