#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <string>
#include <cstring>
#include <queue>
#include <unordered_map>
#include <ctime>
#include <cstdlib>

#define MAX_PKT 16
#define MAX_SEQ 7
#define WINDOW_SIZE 4
#define TIMEOUT 2  // Timeout duration in seconds
#define CRC_POLYNOMIAL 0xEDB88320  // CRC-32 Polynomial
#define PROPAGATION_DELAY 1  // Simulated propagation delay (in seconds)
#define CORRUPT_FRAME_PROBABILITY 3  // Number of frames to corrupt (0 for no corruption)

using namespace std;

typedef unsigned int seq_nr;

typedef struct {
    char data[MAX_PKT];
} packet;

typedef enum { DATA, NAK } frame_kind;

typedef struct {
    frame_kind kind;
    seq_nr seq;
    seq_nr ack;
    packet info;
    uint32_t crc;  // Actual CRC field
} frame;

#define inc(k) (k = (k + 1) % (MAX_SEQ + 1))

std::string sender_name = "Sender";  // Name for Sender
std::string receiver_name = "Receiver";  // Name for Receiver

// Helper function to calculate CRC32
unsigned int calculate_crc(const std::string& data) {
    unsigned int crc = 0xFFFFFFFF;
    for (char byte : data) {
        crc ^= byte << 24;
        for (int i = 0; i < 8; ++i) {
            if (crc & 0x80000000) {
                crc = (crc << 1) ^ CRC_POLYNOMIAL;
            }
            else {
                crc <<= 1;
            }
        }
    }
    return ~crc;
}

// Function to simulate frame corruption (flip some bits in the data)
void corrupt_frame(frame& f) {
    int data_size = sizeof(f.info.data) / sizeof(f.info.data[0]);
    int corruption_bit = rand() % data_size;  // Random bit position to corrupt

    f.info.data[corruption_bit] ^= 0xFF;  // Flip all bits in that byte
}

// Network Layer (Receiver ensures the packets are delivered in order)
class NetworkLayer {
public:
    // Function to simulate delivering in-order packets to the application layer
    static void deliver_in_order(unordered_map<seq_nr, frame>& buffer, seq_nr& expected_seq) {
        while (buffer.find(expected_seq) != buffer.end()) {
            frame& f = buffer[expected_seq];
            cout << receiver_name << ": Delivering packet " << f.seq << " to the application layer. Data: " << f.info.data << endl;
            buffer.erase(expected_seq);  // Remove the delivered packet
            expected_seq = (expected_seq + 1) % (MAX_SEQ + 1);  // Increment expected sequence number
        }
    }
};

// Sender
class Sender {
private:
    vector<frame> buffer;  // Sender's window buffer
    queue<frame> resend_queue;  // Queue for retransmitting frames
    seq_nr base = 0;  // Base sequence number in the window
    seq_nr next_seq_num = 0;  // Next sequence number to send

public:
    Sender() {
        for (seq_nr i = 0; i < 10; ++i) {
            frame f;
            f.seq = i;
            snprintf(f.info.data, sizeof(f.info.data), "Data %d", i);
            f.crc = calculate_crc(f.info.data);
            f.kind = DATA;
            f.ack = 0;  // Initially no acknowledgment
            buffer.push_back(f);
        }
    }

    void addToResendQueue(frame& f) {
        resend_queue.push(f);
    }

    bool getFromResendQueue(frame& f) {
        if (!resend_queue.empty()) {
            f = resend_queue.front();
            resend_queue.pop();
            return true;
        }
        return false;
    }

    vector<frame>& get_buffer() {
        return buffer;
    }

    void send_frames() {
        while (true) {
            while (next_seq_num < base + WINDOW_SIZE && next_seq_num < buffer.size()) {
                frame& f = buffer[next_seq_num];
                cout << sender_name << ": Sending frame " << f.seq << " with data: " << f.info.data << " and CRC: " << f.crc << endl;
                addToResendQueue(f);
                next_seq_num++;
            }

            frame f;
            if (getFromResendQueue(f)) {
                this_thread::sleep_for(chrono::seconds(TIMEOUT));

                if (f.ack == (base % (MAX_SEQ + 1))) {
                    cout << sender_name << ": ACK received for frame " << f.seq << endl;
                    base = (f.ack + 1) % (MAX_SEQ + 1);
                }
                else {
                    cout << sender_name << ": Timeout for frame " << f.seq << endl;
                    cout << sender_name << ": Retransmitting frame " << f.seq << endl;
                    addToResendQueue(f);  // Retransmit the same frame on NAK
                }
            }
        }
    }

    bool isResendQueueEmpty() const {
        return resend_queue.empty();
    }
};

// Receiver
class Receiver {
private:
    unordered_map<seq_nr, frame> buffer;  // Receiver's buffer for out-of-order frames
    queue<seq_nr> ack_queue;  // Acknowledgments queue
    seq_nr expected_seq = 0;  // Expected sequence number for in-order delivery

public:
    Receiver() {}

    void receive_frames(Sender& sender) {
        while (true) {
            if (!sender.isResendQueueEmpty()) {
                frame f;
                if (sender.getFromResendQueue(f)) {
                    unsigned int calculated_crc = calculate_crc(f.info.data);
                    if (calculated_crc != f.crc) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        cout << receiver_name << ": Error in frame " << f.seq << " crc bits: " << calculated_crc << ". Frame corrupted. Sending NAK" << endl;
                        f.kind = NAK;
                        ack_queue.push(-1);  // Send NAK for corrupted frame
                    }
                    else {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        cout << receiver_name << ": Received frame " << f.seq << " with data: " << f.info.data << endl;
                        if (f.seq == expected_seq) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(20));
                            cout << receiver_name << ": Delivering frame " << f.seq << " to network layer." << endl;
                            NetworkLayer::deliver_in_order(buffer, expected_seq);  // Deliver the frame immediately
                            expected_seq = (expected_seq + 1) % (MAX_SEQ + 1);  // Increment expected sequence number
                        }
                        else {
                            cout << receiver_name << ": Out-of-order frame " << f.seq << ". Buffering it." << endl;
                            buffer[f.seq] = f;  // Buffer the out-of-order frame
                        }

                        // Send acknowledgment
                        f.kind = DATA;
                        f.ack = f.seq;  // Piggyback the acknowledgment
                        ack_queue.push(f.seq);  // Send ACK for the received frame
                    }
                }
            }

            while (!ack_queue.empty()) {
                seq_nr seq_num = ack_queue.front();
                ack_queue.pop();
                this_thread::sleep_for(chrono::seconds(PROPAGATION_DELAY));  // Simulate channel delay
                if (seq_num == -1) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    cout << receiver_name << ": Sending NAK" << endl;
                }
                else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    cout << receiver_name << ": Sending ACK for frame " << seq_num << endl;
                }
            }

            // Deliver in-order packets to application layer
            NetworkLayer::deliver_in_order(buffer, expected_seq);
        }
    }
};

int main() {
    srand(time(NULL));  // Seed for random number generation

    Sender sender;
    Receiver receiver;

    for (int i = 0; i < CORRUPT_FRAME_PROBABILITY; ++i) {
        int corrupt_frame_index = rand() % sender.get_buffer().size();
        corrupt_frame(sender.get_buffer()[corrupt_frame_index]);
    }

    thread receiver_thread(&Receiver::receive_frames, &receiver, ref(sender));

    sender.send_frames();

    receiver_thread.join();

    return 0;
}
