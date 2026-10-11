
set(CMAKE_AUTOMOC ON)
if(SERVER)
    if(Qt6HttpServer_FOUND)
        target_compile_definitions(QSmartAssistant PUBLIC -DSERVER)
        target_sources(QSmartAssistant PRIVATE server.h server.cpp)
        # 网页资源：只打包真正被 index.html 引用到的文件
        qt_add_resources(QSmartAssistant "web"
            PREFIX "/"
            FILES
                Web/index.html
                Web/favicon.ico
                Web/src/base.js
                Web/src/style.css
                Web/src/human.png
                Web/src/robot.png
        )
    endif()
endif()
