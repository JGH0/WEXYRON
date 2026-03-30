#ifndef NETWORK_MANAGER_HPP
#define NETWORK_MANAGER_HPP

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <glm/glm.hpp>

// Forward declarations
class NetworkConnection;
class NetworkPacket;

/**
 * @brief Network management system
 * 
 * Handles networking operations including:
 * - Client-server communication
 * - Peer-to-peer connections
 * - Packet serialization
 * - Network events
 */
class NetworkManager {
public:
    enum class NetworkMode {
        None,
        Client,
        Server,
        PeerToPeer
    };
    
    enum class ConnectionState {
        Disconnected,
        Connecting,
        Connected,
        Disconnecting
    };
    
    struct NetworkConfig {
        NetworkMode mode;
        std::string serverAddress;
        int serverPort;
        int maxClients;
        float timeout;
        bool enableReliable;
        bool enableUnreliable;
        
        NetworkConfig() : mode(NetworkMode::None), serverAddress("localhost"),
                         serverPort(8080), maxClients(32), timeout(30.0f),
                         enableReliable(true), enableUnreliable(true) {}
    };

private:
    NetworkConfig config;
    NetworkMode currentMode;
    ConnectionState connectionState;
    
    std::vector<std::shared_ptr<NetworkConnection>> connections;
    std::unordered_map<std::string, std::function<void(const NetworkPacket&)>> packetHandlers;
    
    bool isInitialized;

public:
    NetworkManager();
    ~NetworkManager();
    
    // Network manager lifecycle
    bool initialize(const NetworkConfig& config);
    void shutdown();
    void update(float deltaTime);
    
    // Connection management
    bool startServer(int port = 8080);
    bool connectToServer(const std::string& address, int port = 8080);
    void disconnect();
    void disconnectClient(int clientId);
    
    // Peer-to-peer
    bool startPeerToPeer(int port = 8080);
    bool connectToPeer(const std::string& address, int port = 8080);
    
    // Packet sending
    void sendPacket(const NetworkPacket& packet, bool reliable = true);
    void sendPacketToClient(int clientId, const NetworkPacket& packet, bool reliable = true);
    void broadcastPacket(const NetworkPacket& packet, bool reliable = true);
    
    // Packet handling
    void registerPacketHandler(const std::string& packetType, std::function<void(const NetworkPacket&)> handler);
    void unregisterPacketHandler(const std::string& packetType);
    
    // State queries
    NetworkMode getMode() const { return currentMode; }
    ConnectionState getConnectionState() const { return connectionState; }
    bool isConnected() const { return connectionState == ConnectionState::Connected; }
    bool isServer() const { return currentMode == NetworkMode::Server; }
    bool isClient() const { return currentMode == NetworkMode::Client; }
    
    // Connection access
    std::shared_ptr<NetworkConnection> getConnection(int clientId);
    std::vector<std::shared_ptr<NetworkConnection>> getConnections() const { return connections; }
    int getClientCount() const { return connections.size(); }
    
    // Configuration
    const NetworkConfig& getConfig() const { return config; }
    void setConfig(const NetworkConfig& newConfig);
    
    // Events
    std::function<void(int)> onClientConnected;
    std::function<void(int)> onClientDisconnected;
    std::function<void(const NetworkPacket&)> onPacketReceived;
    std::function<void(ConnectionState, ConnectionState)> onConnectionStateChanged;

private:
    void handleIncomingPackets();
    void updateConnections(float deltaTime);
    void processPacket(const NetworkPacket& packet, int senderId = -1);
};

/**
 * @brief Network connection representation
 */
class NetworkConnection {
private:
    int clientId;
    std::string address;
    int port;
    NetworkManager::ConnectionState state;
    float lastActivityTime;
    float connectionTime;
    
    // Statistics
    int packetsSent;
    int packetsReceived;
    float bytesSent;
    float bytesReceived;

public:
    NetworkConnection(int id, const std::string& address, int port);
    virtual ~NetworkConnection() = default;
    
    // Connection info
    int getClientId() const { return clientId; }
    const std::string& getAddress() const { return address; }
    int getPort() const { return port; }
    NetworkManager::ConnectionState getState() const { return state; }
    void setState(NetworkManager::ConnectionState newState) { state = newState; }
    
    // Timing
    float getLastActivityTime() const { return lastActivityTime; }
    void updateActivity() { lastActivityTime = connectionTime; }
    float getConnectionTime() const { return connectionTime; }
    void updateConnectionTime(float deltaTime) { connectionTime += deltaTime; }
    
    // Statistics
    int getPacketsSent() const { return packetsSent; }
    int getPacketsReceived() const { return packetsReceived; }
    float getBytesSent() const { return bytesSent; }
    float getBytesReceived() const { return bytesReceived; }
    void incrementPacketsSent() { packetsSent++; }
    void incrementPacketsReceived() { packetsReceived++; }
    void addBytesSent(float bytes) { bytesSent += bytes; }
    void addBytesReceived(float bytes) { bytesReceived += bytes; }
    
    // Operations
    virtual bool sendPacket(const NetworkPacket& packet) = 0;
    virtual bool receivePacket(NetworkPacket& packet) = 0;
    virtual void update(float deltaTime) {}
    virtual void close() {}
};

/**
 * @brief Network packet for data transmission
 */
class NetworkPacket {
private:
    std::string type;
    std::vector<uint8_t> data;
    int senderId = -1;
    bool reliable = true;

public:
    NetworkPacket(const std::string& type = "");
    NetworkPacket(const std::string& type, const std::vector<uint8_t>& data);
    
    // Packet properties
    const std::string& getType() const { return type; }
    void setType(const std::string& newType) { type = newType; }
    const std::vector<uint8_t>& getData() const { return data; }
    void setData(const std::vector<uint8_t>& newData) { data = newData; }
    int getSenderId() const { return senderId; }
    void setSenderId(int id) { senderId = id; }
    bool getReliable() const { return reliable; }
    void setReliable(bool rel) { reliable = rel; }
    
    // Data serialization
    template<typename T>
    void write(const T& value);
    
    template<typename T>
    T read();
    
    void writeString(const std::string& str);
    std::string readString();
    
    void writeFloat(float value);
    float readFloat();
    
    void writeInt(int value);
    int readInt();
    
    void writeBool(bool value);
    bool readBool();
    
    void writeVector3(const glm::vec3& vec);
    glm::vec3 readVector3();
    
    // Utility
    void clear();
    size_t getSize() const { return data.size(); }
    bool isEmpty() const { return data.empty(); }

private:
    void writeBytes(const uint8_t* bytes, size_t count);
    void readBytes(uint8_t* bytes, size_t count);
};

#endif
