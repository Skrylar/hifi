//
//  ScreenshareScriptingInterface.cpp
//  interface/src/scripting/
//
//  Created by Milad Nazeri and Zach Fox on 2019-10-23.
//  Copyright 2019 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include <QCoreApplication>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QThread>
#include <QUrl>
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcessEnvironment>

#include <AddressManager.h>
#include <EntityTreeRenderer.h>
#include <EntityTree.h>

#include "EntityScriptingInterface.h"
#include "ScreenshareScriptingInterface.h"

#include <RenderableEntityItem.h>
#include <RenderableTextEntityItem.h>
#include <RenderableWebEntityItem.h>

ScreenshareScriptingInterface::ScreenshareScriptingInterface() {
};

ScreenshareScriptingInterface::~ScreenshareScriptingInterface() {
    stopScreenshare();
}

static const EntityTypes::EntityType LOCAL_SCREENSHARE_WEB_ENTITY_TYPE = EntityTypes::Web;
static const uint8_t LOCAL_SCREENSHARE_WEB_ENTITY_FPS = 30;
static const glm::vec3 LOCAL_SCREENSHARE_WEB_ENTITY_LOCAL_POSITION(0.0f, -0.0862f, 0.0311f);
static const QString LOCAL_SCREENSHARE_WEB_ENTITY_URL = "https://hifi-content.s3.amazonaws.com/Experiences/Releases/usefulUtilities/smartBoard/screenshareViewer/screenshareClient.html?1";
static const glm::vec3 LOCAL_SCREENSHARE_WEB_ENTITY_DIMENSIONS(4.0419f, 2.2735f, 0.0100f);
void ScreenshareScriptingInterface::startScreenshare(const QUuid& screenshareZoneID, const QUuid& smartboardEntityID, const bool& isPresenter) {
    if (QThread::currentThread() != thread()) {
        // We must start a new QProcess from the main thread.
        QMetaObject::invokeMethod(
            this, "startScreenshare",
            Q_ARG(const QUuid&, screenshareZoneID),
            Q_ARG(const QUuid&, smartboardEntityID),
            Q_ARG(const bool&, isPresenter)
        );
        return;
    }

    if (isPresenter && _screenshareProcess && _screenshareProcess->state() != QProcess::NotRunning) {
        qDebug() << "Screenshare process already running. Aborting...";
        return;
    }

    if (isPresenter) {
        _screenshareProcess.reset(new QProcess(this));

        QFileInfo screenshareExecutable(SCREENSHARE_EXE_PATH);
        if (!screenshareExecutable.exists() || !screenshareExecutable.isFile()) {
            qDebug() << "Screenshare executable doesn't exist at" << SCREENSHARE_EXE_PATH;
            return;
        }
    }

    QUuid currentDomainID = DependencyManager::get<AddressManager>()->getDomainID();
    // `https://metaverse.highfidelity.com/api/v1/domain/:domain_id/screenshare`,
    // passing the Domain ID that the user is connected to, as well as the `roomName`.
    // The server will respond with the relevant OpenTok Token, Session ID, and API Key.
    // Upon error-free response, do the logic below, passing in that info as necessary.
    QNetworkAccessManager* manager = new QNetworkAccessManager();
    QObject::connect(manager, &QNetworkAccessManager::finished, this, [=](QNetworkReply *reply) {
        if (reply->error()) {
            qDebug() << "\n\n MN HERE: REPLY" << reply->errorString();
            return;
        }

        QString answer = reply->readAll();
        qDebug() << "\n\n MN HERE: REPLY" << answer;

        
        QString token = "";
        QString apiKey = "";
        QString sessionID = "";

        if (isPresenter) {
            QStringList arguments;
            arguments << "--token=" + token; 
            arguments << "--apiKey=" + apiKey; 
            arguments << "--sessionID=" + sessionID;
            
            connect(_screenshareProcess.get(), &QProcess::errorOccurred,
                [=](QProcess::ProcessError error) { qDebug() << "ZRF QProcess::errorOccurred. `error`:" << error; });
            connect(_screenshareProcess.get(), &QProcess::started, [=]() { qDebug() << "ZRF QProcess::started"; });
            connect(_screenshareProcess.get(), &QProcess::stateChanged,
                [=](QProcess::ProcessState newState) { qDebug() << "ZRF QProcess::stateChanged. `newState`:" << newState; });
            connect(_screenshareProcess.get(), QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                [=](int exitCode, QProcess::ExitStatus exitStatus) {
                    qDebug() << "ZRF QProcess::finished. `exitCode`:" << exitCode << "`exitStatus`:" << exitStatus;
                    emit screenshareStopped();
                });

            _screenshareProcess->start(SCREENSHARE_EXE_PATH, arguments);
        }

        if (!_screenshareViewerLocalWebEntityUUID.isNull()) {
            return;
        }

        auto esi = DependencyManager::get<EntityScriptingInterface>();
        if (!esi) {
            return;
        }

        EntityItemProperties localScreenshareWebEntityProps;
        localScreenshareWebEntityProps.setType(LOCAL_SCREENSHARE_WEB_ENTITY_TYPE);
        localScreenshareWebEntityProps.setMaxFPS(LOCAL_SCREENSHARE_WEB_ENTITY_FPS);
        localScreenshareWebEntityProps.setLocalPosition(LOCAL_SCREENSHARE_WEB_ENTITY_LOCAL_POSITION);
        localScreenshareWebEntityProps.setSourceUrl(LOCAL_SCREENSHARE_WEB_ENTITY_URL);
        localScreenshareWebEntityProps.setParentID(smartboardEntityID);
        localScreenshareWebEntityProps.setDimensions(LOCAL_SCREENSHARE_WEB_ENTITY_DIMENSIONS);

        // EntityPropertyFlags desiredSmartboardProperties;
        // desiredSmartboardProperties += PROP_POSITION;
        // desiredSmartboardProperties += PROP_DIMENSIONS;
        // EntityItemProperties smartboardProps = esi->getEntityProperties(smartboardEntityID, desiredSmartboardProperties);

        QString hostType = "local";
        _screenshareViewerLocalWebEntityUUID = esi->addEntity(localScreenshareWebEntityProps, hostType);

        QObject::connect(esi.data(), &EntityScriptingInterface::webEventReceived, this, [&](const QUuid& entityID, const QVariant& message) {
            if (entityID == _screenshareViewerLocalWebEntityUUID) {
                qDebug() << "ZRF HERE! Inside `webEventReceived(). `entityID`:" << entityID << "`_screenshareViewerLocalWebEntityUUID`:" << _screenshareViewerLocalWebEntityUUID;
                
                auto esi = DependencyManager::get<EntityScriptingInterface>();
                if (!esi) {
                    return;
                }

                QJsonDocument jsonMessage = QJsonDocument::fromVariant(message);
                QJsonObject jsonObject = jsonMessage.object();

                qDebug() << "ZRF HERE! Inside `webEventReceived(). `message`:" << message << "`jsonMessage`:" << jsonMessage;

                if (jsonObject["app"] != "screenshare") {
                    return;
                }

                qDebug() << "ZRF HERE! Inside `webEventReceived(). we're still here!";

                if (jsonObject["method"] == "eventBridgeReady") {
                    QJsonObject responseObject;
                    responseObject.insert("app", "screenshare");
                    responseObject.insert("method", "receiveConnectionInfo");
                    QJsonObject responseObjectData;
                    responseObjectData.insert("token", token);
                    responseObjectData.insert("projectAPIKey", apiKey);
                    responseObjectData.insert("sessionID", sessionID);
                    responseObject.insert("data", responseObjectData);

                    qDebug() << "ZRF HERE! Inside `webEventReceived(). `responseObject.toVariantMap()`:" << responseObject.toVariantMap();

                    // Attempt 1, we receive the eventBridge message, but this won't send a message
                    // to that js
                    auto esi = DependencyManager::get<EntityScriptingInterface>();
                    esi->emitScriptEvent(_screenshareViewerLocalWebEntityUUID, responseObject.toVariantMap());

                    // atempt 2, same outcome
                    auto entityTreeRenderer = DependencyManager::get<EntityTreeRenderer>();
                    auto webEntityRenderable = entityTreeRenderer->renderableForEntityId(_screenshareViewerLocalWebEntityUUID);
                    webEntityRenderable->emitScriptEvent(responseObject.toVariantMap());
                }
            }
        });
    });

    QNetworkRequest request;
    QString tokboxURL = QProcessEnvironment::systemEnvironment().value("hifiScreenshareUrl");
    request.setUrl(QUrl(tokboxURL));
    manager->get(request);
};

void ScreenshareScriptingInterface::stopScreenshare() {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, "stopScreenshare");
        return;
    }

    if (_screenshareProcess && _screenshareProcess->state() != QProcess::NotRunning) {
        _screenshareProcess->terminate();
    }

    if (!_screenshareViewerLocalWebEntityUUID.isNull()) {
        auto esi = DependencyManager::get<EntityScriptingInterface>();
        if (esi) {
            esi->deleteEntity(_screenshareViewerLocalWebEntityUUID);
        }
    }
    _screenshareViewerLocalWebEntityUUID = "{00000000-0000-0000-0000-000000000000}";
}
