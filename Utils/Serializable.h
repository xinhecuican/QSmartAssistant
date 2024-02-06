#ifndef SERIALIZABLE_H
#define SERIALIZABLE_H
#include<QJsonObject>
class Serializable {
public:
    /**
     * @brief 想要使用序列化就要实现这个接口然后调用Serialize::serialize
     */
    virtual void serialized(QJsonObject* json) = 0;
    virtual void deserialized(QJsonObject* json)=0;
    virtual ~Serializable() {};
};

#endif // SERIALIZABLE_H
