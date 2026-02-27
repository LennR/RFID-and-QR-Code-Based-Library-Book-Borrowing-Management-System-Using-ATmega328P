#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include "secrets.h"

String receivedData = "";
bool newData = false;

void updateBook(String documentId, String borrowerDocPath, bool isReturning);
String getStudentDocID(String studentNumber);
String searchFirestoreByISBNAndBookID(String isbn, int bookID);
void sendDataToFirestore(const String& firestoreCollection);
void addStudentToDatabase(String studentNumber, String firstName, String middleName, String lastName, int grade);

void setup() {
  Serial.begin(9600);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("esp:esp ready");
}

void loop() {
  while (Serial.available()) {
    char incomingChar = Serial.read();
    if (incomingChar == '\n') {
      newData = true;
      break;
    } 
    else {
      receivedData += incomingChar;
    }
  }

  if (newData) {
    Serial.print("NodeMCU: Received from Arduino: ");
    Serial.println(receivedData);
    if (receivedData.startsWith("SystemStarted")) {
      if (WiFi.status() == WL_CONNECTED) Serial.println("esp:esp ready");
    } 
    else if (receivedData.startsWith("borrow:")) {
      String data = receivedData.substring(7);
      int firstComma = data.indexOf(',');
      int secondComma = data.indexOf(',', firstComma + 1);

      if (firstComma != -1 && secondComma != -1) {
        String isbn = data.substring(0, firstComma);
        int bookID = data.substring(firstComma + 1, secondComma).toInt();
        String studentNumber = data.substring(secondComma + 1);
        studentNumber.trim();

        Serial.print("Received borrow command for ISBN: ");
        Serial.print(isbn);
        Serial.print(", BookID: ");
        Serial.print(bookID);
        Serial.print(", Student Number: ");
        Serial.println(studentNumber);

        String studentDocID = getStudentDocID(studentNumber);
        if (studentDocID.length() > 0) {
          Serial.print("Found student document ID: ");
          Serial.println(studentDocID);
          String bookDocID = searchFirestoreByISBNAndBookID(isbn, bookID);
          if (bookDocID.length() > 0) {
            String borrowerPath = "projects/" + String(firebaseProjectId) + "/databases/(default)/documents/students/" + studentDocID;
            updateBook(bookDocID, borrowerPath, false); // Borrowing
            //Send to Arduino 
            Serial.println("esp:borrow_ok"); //borrowing done
          } 
          else {
            Serial.println("esp:ERROR: Book not found in Firestore.");
          }
        } 
        else {
          Serial.println("esp:ERROR: Student not found with the provided student number.");
        }
      } 
      else {
        Serial.println("esp:ERROR: Invalid borrow command format.");
      }
    } 
    else if (receivedData.startsWith("return:")) {
      String data = receivedData.substring(7);
      int firstComma = data.indexOf(',');
      int secondComma = data.indexOf(',', firstComma + 1);

      if (firstComma != -1 && secondComma != -1) {
        String isbn = data.substring(0, firstComma);
        int bookID = data.substring(firstComma + 1, secondComma).toInt();
        String studentNumber = data.substring(secondComma + 1);
        studentNumber.trim();
        
        Serial.print("Received return command for ISBN: ");
        Serial.print(isbn);
        Serial.print(", BookID: ");
        Serial.print(bookID);
        Serial.print(", Student Number: ");
        Serial.println(studentNumber);
        
        String bookDocID = searchFirestoreByISBNAndBookID(isbn, bookID);
        if (bookDocID.length() > 0) {
          updateBook(bookDocID, "", true); // Returning
          //Send to Arduino
          Serial.println("esp:return_ok"); //returning done
        } else {
          Serial.println("esp:ERROR: Book not found in Firestore.");
        }
      } else {
        Serial.println("esp:ERROR: Invalid return command format.");
      }
    } 
    else if (receivedData.startsWith("searchBook:")) {
      String data = receivedData.substring(11);
      int firstComma = data.indexOf(',');

      if (firstComma != -1) {
        String isbn = data.substring(0, firstComma);
        int bookID = data.substring(firstComma + 1).toInt();
        
        Serial.print("Received return command for ISBN: ");
        Serial.print(isbn);
        Serial.print(", BookID: ");
        Serial.println(bookID);
        
        String bookDocID = searchFirestoreByISBNAndBookID(isbn, bookID);
      } else {
        Serial.println("esp:ERROR: Invalid command format.");
      }
    } 
    else if (receivedData.startsWith("student:")) {
      String data = receivedData.substring(8);
      int firstComma = data.indexOf(',');
      int secondComma = data.indexOf(',', firstComma + 1);
      int thirdComma = data.indexOf(',', secondComma + 1);
      int fourthComma = data.indexOf(',', thirdComma + 1);

      if (firstComma != -1 && secondComma != -1 && thirdComma != -1 && fourthComma != -1) {
        String firstName = data.substring(0, firstComma);
        String middleName = data.substring(firstComma + 1, secondComma);
        String lastName = data.substring(secondComma + 1, thirdComma);
        String studentNumber = data.substring(thirdComma + 1, fourthComma);
        int grade = data.substring(fourthComma + 1).toInt();

        firstName.trim();
        middleName.trim();
        lastName.trim();
        studentNumber.trim();

        Serial.print("Received create command for: ");
        Serial.print(firstName);
        Serial.print(" ");
        Serial.println(lastName);
        String studentDocID = getStudentDocID(studentNumber);
        if (studentDocID.length() > 0){
          Serial.println("esp:Student Found");
        } else{
          addStudentToDatabase(studentNumber, firstName, middleName, lastName, grade);
        }
      } else {
        Serial.println("esp:ERROR: Invalid create command format.");
      }
    } 
    else if (receivedData.startsWith("addBook:")) {
      String data = receivedData.substring(8);
      int comma1 = data.indexOf(',');
      int comma2 = data.indexOf(',', comma1 + 1);
      int comma3 = data.indexOf(',', comma2 + 1);
      int comma4 = data.indexOf(',', comma3 + 1);
      int comma5 = data.indexOf(',', comma4 + 1);

      if (comma1 != -1 && comma2 != -1 && comma3 != -1 && comma4 != -1 && comma5 != -1) {
        String isbn = data.substring(0, comma1);
        int bookID = data.substring(comma1 + 1, comma2).toInt();
        String title = data.substring(comma2 + 1, comma3);
        String author = data.substring(comma3 + 1, comma4);
        String publisher = data.substring(comma4 + 1, comma5);
        String publishingDate = data.substring(comma5 + 1);

        isbn.trim();
        title.trim();
        author.trim();
        publisher.trim();
        publishingDate.trim();

        Serial.print("Received addbook command for: ");
        Serial.println(title);
        
        addBookToDatabase(isbn, bookID, title, author, publisher, publishingDate);
      } else {
        Serial.println("esp:ERROR: Invalid addbook command format. Expected ISBN,BookID,Title,Author,Publisher,PublishingDate");
      }
    } 
    else {
      Serial.println("NodeMCU: Unknown data format received.");
    }

    receivedData = "";
    newData = false;
  }
  
}
// Function to find the student's document ID by student_number
String getStudentDocID(String studentNumber) {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setInsecure(); 

    HTTPClient http;
    String queryURL = "https://firestore.googleapis.com/v1/projects/" +
                          String(firebaseProjectId) + "/databases/(default)/documents:runQuery?key=" + String(firestoreApiKey);

    Serial.print("Connecting to URL: ");
    Serial.println(queryURL);

    http.begin(client, queryURL);
    http.addHeader("Content-Type", "application/json");

    StaticJsonDocument<512> queryDoc;
    JsonObject structuredQuery = queryDoc.createNestedObject("structuredQuery");
    JsonArray from = structuredQuery.createNestedArray("from");
    from.createNestedObject()["collectionId"] = "students";
    
    JsonObject where = structuredQuery.createNestedObject("where");
    JsonObject fieldFilter = where.createNestedObject("fieldFilter");
    fieldFilter.createNestedObject("field")["fieldPath"] = "student_number";
    fieldFilter["op"] = "EQUAL";
    fieldFilter.createNestedObject("value")["stringValue"] = studentNumber;
    
    String requestBody;
    serializeJson(queryDoc, requestBody);

    Serial.print("Sending query body: ");
    Serial.println(requestBody);

    int httpResponseCode = http.POST(requestBody);

    if (httpResponseCode > 0) {
      String response = http.getString();
      StaticJsonDocument<1024> responseDoc;
      DeserializationError error = deserializeJson(responseDoc, response);

      if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        return "";
      }

      if (responseDoc.size() > 0 && responseDoc[0].containsKey("document")) {
        String documentName = responseDoc[0]["document"]["name"].as<String>();
        int lastSlashIndex = documentName.lastIndexOf('/');
        return documentName.substring(lastSlashIndex + 1);
      }
    } else {
      Serial.print("Error querying student: ");
      Serial.println(httpResponseCode);
      Serial.println(http.errorToString(httpResponseCode));
    }
    http.end();
  }
  return "";
}
// Function to update a document in the books collection
void updateBook(String documentId, String borrowerDocPath, bool isReturning) {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    String firestoreCollection = "books";
    String patchUrl = "https://firestore.googleapis.com/v1/projects/" + String(firebaseProjectId) + 
                      "/databases/(default)/documents/" + firestoreCollection + "/" + documentId + 
                      "?key=" + String(firestoreApiKey) + "&updateMask.fieldPaths=borrower&updateMask.fieldPaths=is_available";

    http.begin(client, patchUrl);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-HTTP-Method-Override", "PATCH");

    StaticJsonDocument<256> doc;
    doc["fields"]["is_available"]["booleanValue"] = isReturning;

    if (isReturning) {
      doc["fields"]["borrower"]["referenceValue"] = borrowerDocPath;
    } else {
      doc["fields"]["borrower"]["nullValue"] = NULL;
    }

    String requestBody;
    serializeJson(doc, requestBody);
    int httpResponseCode = http.POST(requestBody);

    if (httpResponseCode > 0) {
      // Serial.print("HTTP Response code (PATCH): ");
      // Serial.println(httpResponseCode);
      // Serial.print("Firestore Response (PATCH): ");
      // Serial.println(http.getString());
    } else {
      Serial.print("Error on sending PATCH to Firestore: ");
      Serial.println(httpResponseCode);
      Serial.println(http.errorToString(httpResponseCode));
    }
    http.end();
  } else {
    Serial.println("WiFi Disconnected (PATCH)");
  }
}
//search Firestore for a document with a matching ISBN field and bookID
String searchFirestoreByISBNAndBookID(String isbn, int bookID) {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    String firestoreCollection = "books";
    String firestoreRunQueryURL = "https://firestore.googleapis.com/v1/projects/" +
                                  String(firebaseProjectId) + "/databases/(default)/documents:runQuery?key=" + String(firestoreApiKey);
                                  
    http.begin(client, firestoreRunQueryURL);
    http.addHeader("Content-Type", "application/json");

    StaticJsonDocument<512> queryDoc;
    JsonObject structuredQuery = queryDoc.createNestedObject("structuredQuery");
    JsonArray from = structuredQuery.createNestedArray("from");
    from.createNestedObject()["collectionId"] = firestoreCollection;
    
    JsonObject where = structuredQuery.createNestedObject("where");
    JsonObject compositeFilter = where.createNestedObject("compositeFilter");
    compositeFilter["op"] = "AND";
    JsonArray filters = compositeFilter.createNestedArray("filters");
    
    // Filter 1: ISBN
    JsonObject filter1 = filters.createNestedObject();
    JsonObject fieldFilter1 = filter1.createNestedObject("fieldFilter");
    fieldFilter1.createNestedObject("field")["fieldPath"] = "isbn";
    fieldFilter1["op"] = "EQUAL";
    fieldFilter1.createNestedObject("value")["stringValue"] = isbn;
    
    // Filter 2: bookID
    JsonObject filter2 = filters.createNestedObject();
    JsonObject fieldFilter2 = filter2.createNestedObject("fieldFilter");
    fieldFilter2.createNestedObject("field")["fieldPath"] = "bookID";
    fieldFilter2["op"] = "EQUAL";
    fieldFilter2.createNestedObject("value")["integerValue"] = bookID;
    
    String requestBody;
    serializeJson(queryDoc, requestBody);

    Serial.print("\nSending compound query to Firestore: ");
    Serial.println(requestBody);

    int httpResponseCode = http.POST(requestBody);

    if (httpResponseCode > 0) {
      String response = http.getString();
      StaticJsonDocument<1024> responseDoc;
      DeserializationError error = deserializeJson(responseDoc, response);
      
      if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        return "";
      }
      
      if (responseDoc.size() > 0 && responseDoc[0].containsKey("document")) {
        String documentName = responseDoc[0]["document"]["name"].as<String>();
        int lastSlashIndex = documentName.lastIndexOf('/');
        String documentId = documentName.substring(lastSlashIndex + 1);

        JsonObject fields = responseDoc[0]["document"]["fields"];
        
        const char* title = fields["title"]["stringValue"];
        const char* author = fields["author"]["stringValue"];
        const bool is_available = fields["is_available"]["booleanValue"];

        Serial.print("esp:bookAvailability:");
        Serial.println(is_available);
        return documentId;
      } else {
        Serial.println("esp:No matching document found.");
      }
    } else {
      Serial.print("esp:Error on sending query to Firestore: ");
      Serial.println(httpResponseCode);
      Serial.println(http.errorToString(httpResponseCode));
    }
    http.end();
  } else {
    Serial.println("WiFi Disconnected");
  }
  return "";
}
void addBookToDatabase(String isbn, int bookID, String title, String author, String publisher, String publishingDate) {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    String firestoreCollection = "books";
    String documentId = isbn + "_" + String(bookID);
    
    // Set the document name in the URL for document creation/update (PATCH)
    String createUrl = "https://firestore.googleapis.com/v1/projects/" +
                       String(firebaseProjectId) + "/databases/(default)/documents/" + firestoreCollection + "/" + 
                       documentId + "?key=" + String(firestoreApiKey);

    const int MAX_WORDS = 20; 
    int wordCount = 0;
    String wordAdder = "";
    String titleArray[MAX_WORDS];
    String titleToLower = title;
    titleToLower.toLowerCase();
    auto isDelimiter = [](char c) {
      return (c == ' ' || c == ':' || c == '!' || c == ',' || c == '.' || c == '?' || 
              c == '(' || c == ')' || c == '-');
    };

    for (int titleCharCount = 0; titleCharCount < titleToLower.length(); titleCharCount++) {
      char currentChar = titleToLower.charAt(titleCharCount);

      if (isDelimiter(currentChar)) {
        if (wordAdder.length() > 0 && wordCount < MAX_WORDS) { 
          titleArray[wordCount] = wordAdder;
          wordCount++;
        }
        wordAdder = "";
      } else {
        wordAdder = wordAdder + currentChar;
      }
    }
    if (wordAdder.length() > 0 && wordCount < MAX_WORDS) {
      titleArray[wordCount] = wordAdder;
      wordCount++;
    }

    http.begin(client, createUrl);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-HTTP-Method-Override", "PATCH"); 

    StaticJsonDocument<1024> doc;
    JsonObject fields = doc.createNestedObject("fields");
    fields["author"]["stringValue"] = author;
    fields["bookID"]["integerValue"] = bookID;
    fields["borrower"]["nullValue"] = NULL;
    fields["is_available"]["booleanValue"] = true;
    fields["isbn"]["stringValue"] = isbn;
    fields["publisher"]["stringValue"] = publisher;
    fields["publishing_date"]["stringValue"] = publishingDate;
    fields["title"]["stringValue"] = title;

    JsonObject titleArrayField = fields.createNestedObject("title_array");
    JsonObject arrayValue = titleArrayField.createNestedObject("arrayValue");
    JsonArray arrayElements = arrayValue.createNestedArray("values");

    for (int countArray = 0; countArray < wordCount; countArray++) {
      JsonObject element = arrayElements.createNestedObject();
      element["stringValue"] = titleArray[countArray];
    }  

    String requestBody;
    serializeJson(doc, requestBody);

    Serial.print("Sending PATCH to create/update book document: ");
    Serial.println(requestBody);

    int httpResponseCode = http.POST(requestBody);

    if (httpResponseCode > 0) {
      Serial.print("HTTP Response code (PATCH Book): ");
      Serial.println(httpResponseCode);
      Serial.println("esp:New book document created/updated successfully.");
      
      Serial.println("esp:addBook_ok");
    } else {
      Serial.print("esp:Error creating/updating book document: ");
      Serial.println(httpResponseCode);
      Serial.println(http.errorToString(httpResponseCode));
    }
    http.end();
  } else {
    Serial.println("esp:WiFi Disconnected (PATCH Book)");
  }
}
void addStudentToDatabase(String studentNumber, String firstName, String middleName, String lastName, int grade) {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    String createUrl = "https://firestore.googleapis.com/v1/projects/" +
                       String(firebaseProjectId) + "/databases/(default)/documents/students/" + 
                       studentNumber + "?key=" + String(firestoreApiKey);

    http.begin(client, createUrl);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-HTTP-Method-Override", "PATCH");

    StaticJsonDocument<256> createDoc;
    createDoc["fields"]["first_name"]["stringValue"] = firstName;
    createDoc["fields"]["middle_name"]["stringValue"] = middleName;
    createDoc["fields"]["last_name"]["stringValue"] = lastName;
    createDoc["fields"]["student_number"]["stringValue"] = studentNumber;
    createDoc["fields"]["grade"]["integerValue"] = grade;

    String createBody;
    serializeJson(createDoc, createBody);

    Serial.print("Sending PATCH to create document: ");
    Serial.println(createBody);

    int createResponseCode = http.POST(createBody);
    Serial.print("HTTP PATCH response code: ");
    Serial.println(createResponseCode);

    if (createResponseCode > 0) {
      Serial.println("esp:New student document created successfully.");
    } else {
      Serial.print("esp:Error creating student document: ");
      Serial.println(createResponseCode);
      Serial.println(http.errorToString(createResponseCode));
    }
    http.end();
  } else {
    Serial.println("esp:WiFi Disconnected (PATCH)");
  }
}
