


set(CMAKE_AUTOMOC ON)
if(SERVER)
    if(Qt6HttpServer_FOUND)
        target_compile_definitions(QSmartAssistant PUBLIC -DSERVER)
        target_sources(QSmartAssistant PUBLIC server.h server.cpp)
        qt_add_resources(QSmartAssistant "web"
            PREFIX "/"
            FILES 
                Web/index.html
                Web/favicon.ico
                Web/src/base.js
                Web/src/bootstrap.css
                Web/src/bootstrap.js
                Web/src/human.png
                Web/src/jquery.min.js
                Web/src/robot.png
                Web/src/scaffolding.css
        )
    endif()
endif()