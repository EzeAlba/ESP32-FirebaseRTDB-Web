// Then your auth.js can use the global firebase object:
// Al usar los links globales en el HTML se necesita crear los objetos
// la variable global firebase.
const firebaseConfig = {
    apiKey: "AIzaSyDKv1wkflxPSIm-3GBX5__qyY5US3WFI9o",
    authDomain: "esp32-basico-lectura-c3a63.firebaseapp.com",
    databaseURL: "https://esp32-basico-lectura-c3a63-default-rtdb.firebaseio.com",
    projectId: "esp32-basico-lectura-c3a63",
    storageBucket: "esp32-basico-lectura-c3a63.appspot.com",
    messagingSenderId: "477555245069",
    appId: "1:477555245069:web:05c976803e53d0e7e21544"
};

const app = firebase.initializeApp(firebaseConfig);

function writeUserData(userId, name, email, imageUrl) {
    const db = firebase.database();
    db.ref("users/" + userId).set({
        username: name,
        email: email,
        profile_picture: imageUrl
    });
    console.log("Database actualizada");
}

function getUserData(userId) {
    const db = firebase.database();
    const userRef = db.ref("users/" + userId);
    
    // Read data once
    userRef.once('value')
        .then((snapshot) => {
            const userData = snapshot.val();
            console.log("User data:", userData);
            
            // If you want to display specific fields:
            if (userData) {
                console.log("Username:", userData.username);
                console.log("Email:", userData.email);
                console.log("Profile Picture:", userData.profile_picture);
            } else {
                console.log("No data found for user:", userId);
            }
        })
        .catch((error) => {
            console.error("Error reading data:", error);
        });
    
    // Or set up a realtime listener
    userRef.on('value', (snapshot) => {
        console.log("Realtime update:", snapshot.val());
    });
}

writeUserData("Ezequiel", "the best", "ezealba2@gmail.com", "MyImagen");
// Read the data after writing (use setTimeout to ensure write completes)
setTimeout(() => {
    getUserData("Ezequiel");
}, 1000);