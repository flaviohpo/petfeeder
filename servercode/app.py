# import the Flask class from the flask module
from flask import Flask, render_template, request, redirect, Response
import yaml

with open("parameters.yaml", 'r') as stream:
    parameters = yaml.load(stream, Loader=yaml.SafeLoader)

class time:
    def __init__(self, hora=0, min=0, seg=0):
        self.hora = hora
        self.min = min
        self.seg = seg
    def from_string(self, text):
        '''Sets the values passing a string hh:mm:ss as text'''
        self.hora = int(text[0:2])
        self.min = int(text[3:5])
        self.seg = int(text[6:8])
    def __str__(self):
        return f'{self.hora:02d}:{self.min:02d}:{self.seg:02d}'

horario_inicio = time()
horario_fim = time()

horario_inicio.from_string(parameters['hora_inicio'])
horario_fim.from_string(parameters['hora_fim'])

# create the application object
app = Flask(__name__)

# use decorators to link the function to a url
@app.route('/')
def home():
    return render_template('home.html', text1=str(horario_inicio), text2=str(horario_fim))  # render a template

@app.route('/set_value', methods=['POST',])
def set_value():
    '''Utilizado pelo browser'''
    texto_horario_inicio = request.form['horario_inicio']
    texto_horario_fim = request.form['horario_fim']
    horario_inicio.from_string(texto_horario_inicio)
    horario_fim.from_string(texto_horario_fim)
    parameters['hora_inicio'] = str(horario_inicio)
    parameters['hora_fim'] = str(horario_fim)
    with open('parameters.yaml', 'w') as outfile:
        yaml.dump(parameters, outfile, default_flow_style=False)
    return ('', 204)

@app.route('/get_value')
def get_value():
    '''Utilizado pelo hardware'''
    resp = Response("horarios")
    resp.headers['horario_inicio'] = str(horario_inicio)
    resp.headers['horario_fim'] = str(horario_fim)
    return resp

# start the server with the 'run()' method
if __name__ == '__main__':
    app.run(debug=True)
