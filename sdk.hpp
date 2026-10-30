#pragma once 

#include <iostream>
#include <string>
#include <stdexcept>

namespace sdk {

    class client {
        private:
        bool conectado = false;

    public:
        bool conectar() {
            if (conectado)
            return true;
        conectado = true;

        std::cout <<"sdk inicializado corretamente!\n" <<std::endl;
        return true;
    }

    std::string enviar (const std::string& mensagem) {
        if (!conectado) {
            throw std::runtime_error("sdk não inicializado corretamente!");
        }
    
        return "sdk Recebido:" + mensagem;
    }
    bool estaConectado() const {
        return conectado;
    }

    void desconectar() {
        conectado = false;
        std::cout <<"sdk finalizado!\n" <<std::endl;
    }

  };

}
