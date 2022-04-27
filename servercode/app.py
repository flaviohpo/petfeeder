# import the Flask class from the flask module
from flask import Flask, render_template, request, redirect, Response
import yaml
import os.path

class time:
    def __init__(self, hora=0, min=0, seg=0):
        self.__hora = hora
        self.__min = min
        self.__seg = seg
    def from_string(self, text):
        '''Sets the values passing a string hh:mm:ss as text'''
        self.__hora = int(text[0:2])
        self.__min = int(text[3:5])
        self.__seg = int(text[6:8])
    def __str__(self):
        return f'{self.__hora:02d}:{self.__min:02d}:{self.__seg:02d}'
    @property
    def hora(self):
        return self.__hora
    @property
    def min(self):
        return self.__min
    @property
    def seg(self):
        return self.__seg

class horario_alimentacao:

    def __init__(self, time_inicio=time(), duracao=10):
        self.__time_inicio=time_inicio
        self.__duracao=duracao

    @property
    def time_inicio(self):
        return self.__time_inicio

    @property
    def duracao(self):
        return self.__duracao

    def to_dict(self):
        return {"Inicio": self.__time_inicio, "Duracao": self.__duracao}


horario = horario_alimentacao(time(10,10,10),10)

if os.path.isfile('parameters.yaml'):
    print('O arquivo existe')
else:
    print('O arquivo nao existe')
    with open('parameters.yaml', 'w') as outfile:
        yaml.dump({'horario1': horario}, outfile, default_flow_style=False)



#with open("parameters.yaml", 'r') as stream:
#    parameters = yaml.load(stream, Loader=yaml.SafeLoader)

# create the application object
app = Flask(__name__)

# use decorators to link the function to a url
@app.route('/')
def home():
    return "CARAI CACHOERA"
