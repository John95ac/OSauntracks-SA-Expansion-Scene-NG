// christmas-effects.js
(function() {
    let christmasInterval = null; // Variable global para el intervalo



    // Crear efectos de Navidad
    function activateChristmasEffects() {
        let container = document.getElementById('christmas-effects-container');
        if (!container) {
            container = document.createElement('div');
            container.id = 'christmas-effects-container';
            document.body.appendChild(container);
            console.warn('Christmas effects container created dynamically');
        }


        // Limpiar intervalo anterior si existe
        if (christmasInterval) clearInterval(christmasInterval);

        // Falling emojis logic removed per user request
        const emojis = ['❄️', '❄️', '❄️', '❄️', '🎄', '⛄', '🎅', '🎁', '🌟', '☃️', '🐱'];
        christmasInterval = setInterval(() => {
            const emoji = document.createElement('div');
            emoji.textContent = emojis[Math.floor(Math.random() * emojis.length)];
            emoji.style.position = 'absolute';
            emoji.style.left = Math.random() * 100 + 'vw';
            emoji.style.fontSize = (Math.random() * 2 + 1) + 'rem';
            emoji.style.opacity = '0.2';
            emoji.style.filter = 'blur(1px)';
            emoji.style.animation = 'christmas-fall 10s linear forwards';
            container.appendChild(emoji);

            setTimeout(() => emoji.remove(), 10000);
        }, 1000);

        // Agregar emoji fijo de Navidad en esquina inferior derecha
        const fixedChristmasTree = document.createElement('div');
        fixedChristmasTree.textContent = '🎄';
        fixedChristmasTree.style.position = 'fixed';
        fixedChristmasTree.style.bottom = '-10px';
        fixedChristmasTree.style.right = '20px';
        fixedChristmasTree.style.fontSize = '4rem';
        fixedChristmasTree.style.opacity = '0.3';
        fixedChristmasTree.style.filter = 'blur(0.5px)';
        fixedChristmasTree.style.zIndex = '1';
        fixedChristmasTree.style.pointerEvents = 'none';
        container.appendChild(fixedChristmasTree);
// Agregar segundo emoji fijo de Navidad en esquina inferior izquierda
const fixedChristmasTreeLeft = document.createElement('div');
fixedChristmasTreeLeft.textContent = '🎄';
fixedChristmasTreeLeft.style.position = 'fixed';
fixedChristmasTreeLeft.style.bottom = '-25px';
fixedChristmasTreeLeft.style.left = '10px';
fixedChristmasTreeLeft.style.fontSize = '8rem';
fixedChristmasTreeLeft.style.opacity = '0.3';
fixedChristmasTreeLeft.style.filter = 'blur(0.5px)';
fixedChristmasTreeLeft.style.zIndex = '1';
fixedChristmasTreeLeft.style.pointerEvents = 'none';
container.appendChild(fixedChristmasTreeLeft);


        // CSS para animación
        const style = document.createElement('style');
        style.textContent = `
            @keyframes christmas-fall {
                0% { transform: translateY(-100px) rotate(0deg); }
                100% { transform: translateY(100vh) rotate(360deg); }
            }
        `;
        document.head.appendChild(style);

        // Detener después de 5 horas para no sobrecargar
        setTimeout(() => {
            if (christmasInterval) {
                clearInterval(christmasInterval);
                christmasInterval = null;
            }
        }, 18000000);
    }

    // Función para detener efectos
    function stopChristmasEffects() {
        if (christmasInterval) {
            clearInterval(christmasInterval);
            christmasInterval = null;
        }
        const container = document.getElementById('christmas-effects-container');
        if (container) {
            container.innerHTML = ''; // Limpiar emojis existentes
        }
    }

    // Exponer funciones globales
    window.activateChristmasEffects = activateChristmasEffects;
    window.stopChristmasEffects = stopChristmasEffects;
})();