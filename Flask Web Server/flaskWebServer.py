from flask import Flask, jsonify, request, json     #flask
from datetime import datetime, timedelta
import pymysql                                      #pymysql, sqlalchemy
from flask_sqlalchemy import SQLAlchemy
from sqlalchemy.sql import text
from reportlab.pdfgen import canvas                 #reportlab
from reportlab.lib import colors
from reportlab.lib.pagesizes import landscape, A4
from reportlab.platypus import SimpleDocTemplate, Table, TableStyle, Paragraph, Spacer
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.enums import TA_CENTER
import math
import os
import smtplib                                      
from email.mime.multipart import MIMEMultipart       #mime
from email.mime.base import MIMEBase
from email import encoders
from email.utils import formatdate
from pathlib import Path
import locale
db = SQLAlchemy()

app = Flask(__name__)

locale.setlocale(locale.LC_TIME, 'es_ES')

db_name = '/usdatabase'
dbusername = '*****'
password = '*****'
userpass = 'mysql+pymysql://'+dbusername+':'+password+'@'
server = '127.0.0.1'

app.config['SQLALCHEMY_DATABASE_URI'] = userpass+server+db_name
app.config['SQLALCHEMY_TRACK_MODIFICATIONS'] = True

emailFrom = '*****'
emailTo = ''
logs = ''

db.init_app(app)

@app.route('/', methods=['GET', 'POST'])
def webServerUp():
    return getjson()

def getjson():
    '''
    USERDATA JSON: {state: %s, hq: %s, department: %s, username: %s, username2: %s, surname: %s, surname2: %s, surname3: %s, jobpos: %s, userid: %s, isadmin: %s, msgtype: 'USERDATA_MSG'}
    FILE GENERATION JSON: {email: %s, logs: %s, msgtype: 'FILES_MSG'}
    '''

    if not request.is_json:
        print("Invalid JSON file format")
        return jsonify({ 'error': 'The message obtained does not correspond to a JSON file type'}), 400
    
    string = request.get_json()
    if string['msgtype'] == 'USERDATA_MSG':
        print("USERDATA message type")
        print(string)
        if int(string['isadmin']) == 1:
            workerid = 'NULL'
            adminid = string['userid']
            checktime = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        else:
            workerid = string['userid']
            adminid = 'NULL'
            checktime = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        
        try:
            print("Trying to execute the query...")
            db.session.execute(text("INSERT INTO CHECKINS(workerid, adminid, checktime) VALUES ("+str(workerid)+","+str(adminid)+",'"+checktime+"')"))
            db.session.commit()
            print("Done!")
            return jsonify({'success': 'Checkin correctly inserted into the database'}), 200
        except Exception as e:
            print("Error!")
            return jsonify({'error': e}), 400
        
    if string['msgtype'] == 'FILES_MSG':
        print("FILES message type")
        print(string)
        global logs, emailTo
        logs = string['logs']
        emailTo = string['email']
        try:
            print("Trying to generate files...")
            return generateAndSendFiles()
        except Exception as e:
            print("Error!")
            return jsonify({'error': str(e)}), 500

    if(string['msgtype'] == 'UPDATE_MSG'):
        print("UPDATE message type")
        print(string)
        if int(string['isadmin']) == 1:
            print("Its an admin")
            workerid = 'NULL'
            adminid = string['userid']
        else:
            print("Its not an admin")
            workerid = string['userid']
            adminid = 'NULL'
        
        try:
            if int(string['isadmin']) == 1:
                temp = db.session.execute(text('SELECT email FROM ADMINISTRATORS WHERE administrators.id='+str(adminid))).first()
                print("Admin mail obtained")
                return jsonify({'success': 'Mail correctly acquired from the database', 'email': temp[0]}), 200
            else:
                temp = temp = db.session.execute(text('SELECT email FROM WORKERS WHERE workers.id='+str(workerid))).first()
                print("Worker mail obtained")
                return jsonify({'success': 'Mail correctly acquired from the database', 'email': temp[0]}), 200
        except Exception as e:
            print("Error!")
            return jsonify({'error': e}), 400
    print("Unidentified message type")
    return jsonify({'error': 'Unkown message format'}), 400

def generateAndSendFiles():
    # 'EMAIL=%s, LOGS=%s&%s;...'
    # hacer SELECT ...
    date = datetime.now().strftime('%Y-%m-%d')
    print("Requesting pdf file query...")
    query = text('''SELECT workers.id,
                    CONCAT_WS(\' \', workers.username, workers.username2, workers.surname, workers.surname2, workers.surname3) AS userFullName,
                    jobpositions.jobpos_name,
                    checkins.checktime,
                    \'worker\' AS type
                    FROM checkins JOIN workers ON checkins.workerid = workers.id JOIN jobpositions ON workers.jobpos_id = jobpositions.id
                    UNION ALL
                    SELECT administrators.id,
                    CONCAT_WS(' ', administrators.username, administrators.username2, administrators.surname, administrators.surname2, administrators.surname3) AS userFullName,
                    jobpositions.jobpos_name,
                    checkins.checktime,
                    \'admin\' AS type
                    FROM checkins JOIN administrators ON checkins.adminid = administrators.id JOIN jobpositions ON administrators.jobpos_id = jobpositions.id
                    ORDER BY checktime ASC;''')
    temp = db.session.execute(query).mappings().all()
    print("Formatting pdf file...")
    diccList = list()
    for d in range(len(temp)):
        found = False
        key = temp[d]['id']
        key2 = temp[d]['type']
        for d1 in range(len(diccList)):
            if diccList[d1]['id'] == key and diccList[d1]['type'] == key2:
                diccList[d1]['checktime'].append(temp[d]['checktime'])
                found = True
                break
        if not found:
            newDicc = {
                'type': temp[d]['type'],
                'id': temp[d]['id'],
                'userFullName': temp[d]['userFullName'],
                'jobpos_name': temp[d]['jobpos_name'],
                'checktime': [temp[d]['checktime']]
            }
            diccList.append(newDicc)
    # generar fichero de logs
    # C001&121235;...
    # C-001 2026-02-04 12:12:35
    print("Formatting log file...")
    split = logs.split(';')
    dicc = {}
    for e in range(len(split)):
        split2 = split[e].split('&')
        pos = e
        dateString = split2[0][:1]+'-'+split2[0][1:]+' '+str(date)+' '+split2[1][:2]+':'+split2[1][2:4]+':'+split2[1][4:6]
        dicc[pos] = dateString

    logfilename = 'logFile-' + str(date)+'.txt'
    with open(logfilename, 'a') as f:
        for key in dicc:
            f.write(dicc[key]+'\n')
    # generar el fichero pdf
    # PDF FILE: Nombre, Apellidos, puesto de trabajo, checkins, checkouts, horas trabajadas, tiempo de descanso, Horas a cumplir
    print("Log file generated")
    pdffilename = 'checkinsReport-'+str(date)+'.pdf'
    data = [['Nombre completo', 'Puesto de Trabajo', 'CheckIns totales', 'CheckOuts totales', 'Horas trabajadas', 'Descanso total', 'Horas a cumplir']]

    for dictionary in diccList:
        ls = []
        ls.append(dictionary['userFullName'])
        ls.append(dictionary['jobpos_name'])
        dates = dictionary['checktime']
        ls.append(math.ceil(len(dates)/2))
        ls.append(math.floor(len(dates)/2))
        sumtrab = timedelta()
        sumout = timedelta()
        for i in range(len(dates)-1):
            time = (dates[i+1]-dates[i])
            if i %2 == 0:
                sumtrab += time
            else:
                sumout += time
        ls.append(str(sumtrab))
        ls.append(str(sumout))
        ls.append('08:00:00')
        data.append(ls)
    print('Data list generated')
    print('Last item from the list: ' + str(data[-1]))
    document = SimpleDocTemplate(pdffilename, pagesize=landscape(A4), topMargin=30, bottomMargin=30, leftMargin=30, rightMargin=30)
    colwidths = [160, 160, 100, 100, 100, 100, 100]
    elements = []
    titleStyles = ParagraphStyle("title", alignment=TA_CENTER, fontSize=18, spaceAfter=20)
    titleText = Paragraph('INFORME DE REGISTRO DIARIO DE FICHADO ('+ datetime.now().strftime('%A, %d de %B de %Y') +')', titleStyles)
    table = Table(data, colWidths = colwidths)
    tableStyles = TableStyle([
        ('FONTNAME', (1, -1), (1, -1), 'Courier'),
        ('TEXTCOLOR', (0, 0), (-1, 0), colors.black),
        ('ALIGN', (0, 0), (-1, -1), 'CENTER'),
        ('GRID', (0, 0), (-1, -1), 1, colors.black),
        ('FONTSIZE', (0, 0), (0, -1), 10),
        ('FONTSIZE', (0, 1), (-1, -1), 6),
        ('VALIGN', (0, 0), (-1, -1), 'MIDDLE'),
    ])
    table.setStyle(tableStyles)
    elements.append(titleText)
    elements.append(Spacer(1,12))
    elements.append(table)
    document.build(elements, onFirstPage=watermark, onLaterPages=watermark)
    
    print("Pdf file generated")
    # enviar el fichero pdf al administrador activo indicado en el mensaje

    sent = False
    paths = ['checkinsReport-'+str(date)+'.pdf', 'logFile-'+str(date)+'.txt']
    print("File paths" + str(paths))
    for tries in range(3):
        print("Sending mail... Attempt nº"+ str(tries+1))
        code = sendEmail(emailFrom, [emailTo], paths)
        if code:
            sent = code
            break

    if sent:
        print("Email sent")
        os.remove('checkinsReport-'+str(date)+'.pdf')
        os.remove('logFile-'+str(date)+'.txt')
        print("log and pdf files removed")
        db.session.execute(text('DELETE FROM CHECKINS'))
        db.session.commit()
        print("Checkin table registries removed")
        return jsonify({'success': 'The message has been generated and sent correctly.'}), 200
    else:
        print("Mail not sent")
        return jsonify({'error': 'The files could not be sent via email'}), 400

def watermark(canvas, doc):
    canvas.saveState()
    canvas.setFont('Helvetica-Bold', 50)
    canvas.setFillAlpha(0.3)
    canvas.setFillColor(colors.grey)
    width, height = doc.pagesize
    canvas.translate(width/2,height/2)
    canvas.rotate(45)
    canvas.drawCentredString(0,0,"UNIVERSIDAD DE SEVILLA")
    canvas.restoreState()

def sendEmail(send_from, send_to, files=[], server='*****', port=587, username=emailFrom, pwd='*****', use_tls=True):
    
    subject = 'Report files from data ' + datetime.now().strftime('%Y-%m-%d') + '.'
    mail = MIMEMultipart()
    mail['From'] = send_from
    mail['To'] = ', '.join(send_to)
    mail['Date'] = formatdate(localtime=True)
    mail['Subject'] = subject

    print('Mail subject field: ' + subject)

    for path in files:
        part = MIMEBase('application', "octet-stream")
        with open(path, 'rb') as file:
            part.set_payload(file.read())
        encoders.encode_base64(part)
        part.add_header('Content-Disposition',
                        'attachment; filename={}'.format(Path(path).name))
        mail.attach(part)
    smtp = smtplib.SMTP(server, port)
    print("SMTP connection opened!")
    if use_tls:
        smtp.starttls()
    print("TLS initialized!")
    smtp.login(username, pwd)
    smtp.sendmail(send_from, send_to, mail.as_string())
    smtp.quit()
    return True

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=8081, debug=True)